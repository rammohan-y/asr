#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <memory>
#include <mutex>
#include <map>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <queue>
#include <condition_variable>
#include <functional>
#include <random>
#include "Uuid.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <nlohmann/json.hpp>
#include <vosk_api.h>

using json = nlohmann::json;
typedef websocketpp::server<websocketpp::config::asio> server;
typedef server::message_ptr message_ptr;
using websocketpp::connection_hdl;

constexpr int PORT = 9000;
constexpr int SAMPLE_RATE = 16000;  // Vosk model expects 16kHz

// Global configuration
bool g_save_audio = false;  // Set from SAVE_AUDIO environment variable
std::string g_log_folder = ".";  // Set from LOG_FOLDER environment variable
std::string g_recording_folder = ".";  // Set from RECORDING_FOLDER environment variable

// Global Vosk model (shared across all connections)
VoskModel* g_vosk_model = nullptr;
std::mutex g_model_mutex;

#include "GlobalLogger.h"

// Helper: Generate UUID (simple version)
std::string generate_uuid() { return "asr-" + util::generateUuidV4(); }

// Helper: Get current timestamp string
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

// Helper: Create directory if it doesn't exist
bool create_directory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        // Path exists, check if it's a directory
        return S_ISDIR(st.st_mode);
    }
    
    // Try to create directory
    if (mkdir(path.c_str(), 0755) == 0) {
        return true;
    }
    
    return false;
}

// Convenience wrapper for transcript logging
void log_transcript(const std::string& session_uuid, const std::string& text, const std::string& level, const std::string& call_id = "") {
    // Use callId if available, otherwise use session_uuid
    std::string log_id = call_id.empty() ? session_uuid : call_id;
    getGlobalLogger()->info(log_id, level);
    getGlobalLogger()->debug(log_id, level + " \"" + text + "\"");
}


// WAV file writer for saving audio streams
class WavWriter {
private:
    std::ofstream file;
    std::string filename;
    std::string session_uuid;
    uint32_t data_size;
    std::mutex write_mutex;
    
    void write_header() {
        // WAV file header structure
        struct WavHeader {
            // RIFF chunk
            char riff[4] = {'R', 'I', 'F', 'F'};
            uint32_t file_size;  // File size - 8
            char wave[4] = {'W', 'A', 'V', 'E'};
            
            // fmt chunk
            char fmt[4] = {'f', 'm', 't', ' '};
            uint32_t fmt_size = 16;  // PCM
            uint16_t audio_format = 1;  // PCM
            uint16_t num_channels = 1;  // Mono
            uint32_t sample_rate = SAMPLE_RATE;  // 16kHz
            uint32_t byte_rate = SAMPLE_RATE * 2;  // sample_rate * num_channels * bytes_per_sample
            uint16_t block_align = 2;  // num_channels * bytes_per_sample
            uint16_t bits_per_sample = 16;  // 16-bit
            
            // data chunk
            char data[4] = {'d', 'a', 't', 'a'};
            uint32_t data_size;
        } __attribute__((packed));
        
        WavHeader header;
        header.data_size = data_size;
        header.file_size = 36 + data_size;  // 36 = header size - 8 bytes
        
        file.seekp(0, std::ios::beg);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }
    
public:
    WavWriter(const std::string& uuid) : session_uuid(uuid), data_size(0) {
        // No "audio_" prefix, just UUID.wav
        std::string basename = uuid + ".wav";
        // Use recording folder from environment variable
        filename = g_recording_folder + "/" + basename;
        file.open(filename, std::ios::binary | std::ios::out);
        
        if (file.is_open()) {
            // Write initial header with zero data size
            write_header();
            getGlobalLogger()->info(session_uuid, "Audio recording started: " + filename);
        } else {
            getGlobalLogger()->error(session_uuid, "Failed to create WAV file: " + filename);
        }
    }
    
    void write_audio(const char* data, size_t size) {
        if (!file.is_open()) return;
        
        std::lock_guard<std::mutex> lock(write_mutex);
        file.write(data, size);
        data_size += size;
    }
    
    ~WavWriter() {
        if (file.is_open()) {
            // Update header with final data size
            write_header();
            file.close();
            getGlobalLogger()->info(session_uuid, "Audio saved: " + filename + " (" + std::to_string(data_size) + " bytes)");
        }
    }
    
    std::string get_filename() const { return filename; }
};

// Simple thread pool for offloading Vosk processing
class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;

public:
    explicit ThreadPool(size_t num_threads) : stop(false) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });
                        
                        if (this->stop && this->tasks.empty()) {
                            return;
                        }
                        
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
};

// Global thread pool for Vosk processing
std::unique_ptr<ThreadPool> g_thread_pool;

// Connection state - each connection has its own recognizer
struct ConnectionState {
    std::unique_ptr<VoskRecognizer, decltype(&vosk_recognizer_free)> recognizer;
    std::string client_id;
    std::string session_uuid;     // Unique ID for this ASR session
    std::string call_id;          // Voice Tester Call ID from metadata
    std::string fs_uuid;          // FreeSWITCH UUID from metadata
    std::unique_ptr<WavWriter> wav_writer;  // Optional audio recording
    std::mutex processing_mutex;  // Ensures sequential processing per connection
    bool is_ready;                // Indicates recognizer is fully initialized
    bool metadata_received;       // Indicates if metadata was received
    std::string last_partial_text;  // For deduplication of partial transcripts
    std::string last_final_text;    // For deduplication of final transcripts
    
    ConnectionState() : recognizer(nullptr, vosk_recognizer_free), is_ready(false), metadata_received(false) {}
};

std::map<connection_hdl, std::shared_ptr<ConnectionState>, std::owner_less<connection_hdl>> g_connections;
std::mutex g_connections_mutex;

// Send transcript back to FreeSWITCH via WebSocket
void sendTranscriptToFreeSwitch(server* s, connection_hdl hdl, std::shared_ptr<ConnectionState> conn_state, const std::string& text, bool isFinal) {
    try {
        // Create JSON message with transcript for FreeSWITCH
        json transcriptMsg = {
            {"type", "transcript"},
            {"asr_session_id", conn_state->session_uuid},
            {"call_id", conn_state->call_id},
            {"fs_uuid", conn_state->fs_uuid},
            {"text", text},
            {"final", isFinal},
            {"timestamp", get_timestamp()}
        };
        
        // Send back to FreeSWITCH via WebSocket
        s->send(hdl, transcriptMsg.dump(), websocketpp::frame::opcode::text);
        
        getGlobalLogger()->info(conn_state->session_uuid, 
            "Sent transcript back to FreeSWITCH: " + text + (isFinal ? " (FINAL)" : " (PARTIAL)"));
            
    } catch (const std::exception& e) {
        getGlobalLogger()->error(conn_state->session_uuid, 
            "Failed to send transcript to FreeSWITCH: " + std::string(e.what()));
    }
}

// Send ASR session ID back to FreeSWITCH via WebSocket
void sendAsrSessionIdToFreeSwitch(server* s, connection_hdl hdl, std::shared_ptr<ConnectionState> conn_state) {
    try {
        // Create JSON message with ASR session ID for FreeSWITCH
        json asrSessionMsg = {
            {"type", "asr_session_id"},
            {"asr_session_id", conn_state->session_uuid},
            {"call_id", conn_state->call_id},
            {"fs_uuid", conn_state->fs_uuid},
            {"timestamp", get_timestamp()}
        };
        
        // Send back to FreeSWITCH via WebSocket
        s->send(hdl, asrSessionMsg.dump(), websocketpp::frame::opcode::text);
        
        getGlobalLogger()->info(conn_state->session_uuid, 
            "Sent ASR session ID back to FreeSWITCH: " + conn_state->session_uuid);
            
    } catch (const std::exception& e) {
        getGlobalLogger()->error(conn_state->session_uuid, 
            "Failed to send ASR session ID to FreeSWITCH: " + std::string(e.what()));
    }
}

// WebSocket message handler
void on_message(server* s, connection_hdl hdl, message_ptr msg) {
    try {
        auto payload = msg->get_payload();
        auto opcode = msg->get_opcode();
        
        // Check if it's JSON (text) or binary audio data
        if (opcode == websocketpp::frame::opcode::text) {
            // Get connection state for session UUID
            std::shared_ptr<ConnectionState> conn_state;
            {
                std::lock_guard<std::mutex> lock(g_connections_mutex);
                if (g_connections.count(hdl)) {
                    conn_state = g_connections[hdl];
                }
            }
            
            if (!conn_state) {
                getGlobalLogger()->error("unknown", "No connection state found for text message");
                return;
            }
            
            // Check if this is metadata from mod_audio_stream (JSON with callId and fsUuid)
            try {
                auto j = json::parse(payload);
                
                // Check if this looks like metadata from mod_audio_stream
                if (j.contains("callId") && j.contains("fsUuid")) {
                    // This is metadata from mod_audio_stream
                    conn_state->call_id = j.value("callId", "");
                    conn_state->fs_uuid = j.value("fsUuid", "");
                    conn_state->metadata_received = true;
                    
                    getGlobalLogger()->info(conn_state->session_uuid, 
                        "Metadata received - CallId: " + conn_state->call_id + 
                        ", FsUuid: " + conn_state->fs_uuid);
                    
                    // Log both IDs for tracking with ASR session ID as primary identifier
                    getGlobalLogger()->info(conn_state->session_uuid, 
                        "CallId: " + conn_state->call_id + 
                        " | FreeSWITCH UUID: " + conn_state->fs_uuid);
                    
                    // Send ASR session ID back to FreeSWITCH via WebSocket
                    sendAsrSessionIdToFreeSwitch(s, hdl, conn_state);
                    
                    return; // Don't process as regular JSON command
                }
                
                // Handle regular JSON commands
                std::string msg_type = j.value("type", "");
                
                if (msg_type == "ping") {
                    json response = {
                        {"type", "pong"},
                        {"timestamp", j.value("timestamp", "")}
                    };
                    response["session_uuid"] = conn_state->session_uuid;
                    s->send(hdl, response.dump(), websocketpp::frame::opcode::text);
                }
            } catch (const json::parse_error& e) {
                // Not JSON, might be plain text metadata (fallback)
                std::string text_payload = payload;
                if (text_payload.find("callId") != std::string::npos && 
                    text_payload.find("fsUuid") != std::string::npos) {
                    
                    // Try to extract IDs from plain text (basic parsing)
                    size_t callIdPos = text_payload.find("\"callId\":\"");
                    size_t fsUuidPos = text_payload.find("\"fsUuid\":\"");
                    
                    if (callIdPos != std::string::npos && fsUuidPos != std::string::npos) {
                        callIdPos += 10; // Skip "callId":"
                        size_t callIdEnd = text_payload.find("\"", callIdPos);
                        fsUuidPos += 10; // Skip "fsUuid":"
                        size_t fsUuidEnd = text_payload.find("\"", fsUuidPos);
                        
                        if (callIdEnd != std::string::npos && fsUuidEnd != std::string::npos) {
                            conn_state->call_id = text_payload.substr(callIdPos, callIdEnd - callIdPos);
                            conn_state->fs_uuid = text_payload.substr(fsUuidPos, fsUuidEnd - fsUuidPos);
                            conn_state->metadata_received = true;
                            
                            getGlobalLogger()->info(conn_state->session_uuid, 
                                "Metadata received (plain text) - CallId: " + conn_state->call_id + 
                                ", FsUuid: " + conn_state->fs_uuid);
                            
                            getGlobalLogger()->info(conn_state->session_uuid, 
                                "CallId: " + conn_state->call_id + 
                                " | FreeSWITCH UUID: " + conn_state->fs_uuid);
                            
                            // Send ASR session ID back to FreeSWITCH via WebSocket
                            sendAsrSessionIdToFreeSwitch(s, hdl, conn_state);
                        }
                    }
                }
            }
        }
        else if (opcode == websocketpp::frame::opcode::binary) {
            // Handle binary audio data - expect 16kHz linear PCM int16
            
            // Get connection state (minimize mutex lock time)
            std::shared_ptr<ConnectionState> conn_state;
            {
                std::lock_guard<std::mutex> lock(g_connections_mutex);
                
                if (!g_connections.count(hdl)) {
                    getGlobalLogger()->error("unknown", "Unknown connection");
                    return;
                }
                
                conn_state = g_connections[hdl];
            }  // Release mutex here - don't hold it during Vosk processing!
            
            if (!conn_state || !conn_state->recognizer) {
                std::string uuid = conn_state ? conn_state->session_uuid : "unknown";
                getGlobalLogger()->error(uuid, "Recognizer not initialized");
                return;
            }
            
            // Copy audio data (payload is temporary)
            std::string audio_copy = payload;
            
            // Save audio to WAV file if enabled
            if (conn_state->wav_writer) {
                conn_state->wav_writer->write_audio(audio_copy.c_str(), audio_copy.size());
            }
            
            // Offload Vosk processing to thread pool to keep WebSocket I/O responsive!
            g_thread_pool->enqueue([s, hdl, conn_state, audio_copy]() {
                // Lock this connection's processing mutex to ensure:
                // 1. Recognizer is fully initialized before use
                // 2. Audio packets are processed sequentially (in order)
                std::lock_guard<std::mutex> processing_lock(conn_state->processing_mutex);
                
                // Check if recognizer is ready
                if (!conn_state->is_ready || !conn_state->recognizer) {
                    // Recognizer not ready yet, skip this packet
                    return;
                }
                
                // Receive as-is: should be 16kHz linear PCM int16 from FreeSWITCH
                const char* audio_data = audio_copy.c_str();
                int audio_bytes = audio_copy.size();
                
                // Feed to Vosk (runs on worker thread, not blocking WebSocket I/O)
                // The lock ensures packets are processed in order for this connection
                int result = vosk_recognizer_accept_waveform(
                    conn_state->recognizer.get(),
                    audio_data,
                    audio_bytes
                );
                
                // result == 1 means final result is ready
                // result == 0 means partial result available
                if (result == 1) {
                    // Final result - sentence complete
                    const char* result_json = vosk_recognizer_result(conn_state->recognizer.get());
                    auto result_obj = json::parse(result_json);
                    
                    if (result_obj.contains("text") && !result_obj["text"].get<std::string>().empty()) {
                        std::string text = result_obj["text"];
                        
                        // Check for duplicate final transcript
                        if (conn_state->last_final_text != text) {
                            conn_state->last_final_text = text;
                            
                            log_transcript(conn_state->session_uuid, text, "TRANSCRIPT_FINAL", conn_state->call_id);
                            
                            // Send final transcription to client with session ID
                            json response = {
                                {"type", "transcription"},
                                {"session_uuid", conn_state->session_uuid},
                                {"text", text},
                                {"final", true},
                                {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
                            };
                            
                            try {
                                s->send(hdl, response.dump(), websocketpp::frame::opcode::text);
                            } catch (const std::exception& e) {
                                // Connection may have closed, ignore
                            }
                            
                            // Send transcript back to FreeSWITCH for sip_caller
                            sendTranscriptToFreeSwitch(s, hdl, conn_state, text, true);
                        } else {
                            getGlobalLogger()->debug(conn_state->session_uuid, 
                                "Duplicate final transcript ignored: \"" + text + "\"");
                        }
                    }
                } else {
                    // Partial result - word in progress
                    const char* partial_json = vosk_recognizer_partial_result(conn_state->recognizer.get());
                    auto partial_obj = json::parse(partial_json);
                    
                    if (partial_obj.contains("partial") && !partial_obj["partial"].get<std::string>().empty()) {
                        std::string text = partial_obj["partial"];
                        
                        // Check for duplicate partial transcript
                        if (conn_state->last_partial_text != text) {
                            conn_state->last_partial_text = text;
                            
                            //log_transcript(conn_state->session_uuid, text, "TRANSCRIPT_PARTIAL");
                            
                            // Send partial transcription for real-time feedback with session ID
                            json response = {
                                {"type", "transcription"},
                                {"session_uuid", conn_state->session_uuid},
                                {"text", text},
                                {"final", false},
                                {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
                            };
                            
                            try {
                                s->send(hdl, response.dump(), websocketpp::frame::opcode::text);
                            } catch (const std::exception& e) {
                                // Connection may have closed, ignore
                            }
                            
                            // Send partial transcript back to FreeSWITCH for sip_caller
                            sendTranscriptToFreeSwitch(s, hdl, conn_state, text, false);
                        } else {
                            getGlobalLogger()->debug(conn_state->session_uuid, 
                                "Duplicate partial transcript ignored: \"" + text + "\"");
                        }
                    }
                }
            });
        }
    }
    catch (const json::exception& e) {
        getGlobalLogger()->error("", std::string("JSON error: ") + e.what());
    }
    catch (const std::exception& e) {
        getGlobalLogger()->error("", std::string("Message handler error: ") + e.what());
    }
}

// Connection opened
void on_open(server* s, connection_hdl hdl) {
    auto conn_state = std::make_shared<ConnectionState>();
    
    // Generate UUID for this ASR session
    conn_state->session_uuid = generate_uuid();
    getGlobalLogger()->info(conn_state->session_uuid, "Session created");
    
    // Create WAV writer if audio saving is enabled
    if (g_save_audio) {
        conn_state->wav_writer = std::make_unique<WavWriter>(conn_state->session_uuid);
    }
    
    // Create a new recognizer for this connection
    {
        getGlobalLogger()->info(conn_state->session_uuid, "Initializing recognizer");
        std::lock_guard<std::mutex> model_lock(g_model_mutex);
        VoskRecognizer* rec = vosk_recognizer_new(g_vosk_model, SAMPLE_RATE);
        
        // Enable word-level results
        vosk_recognizer_set_max_alternatives(rec, 0);
        vosk_recognizer_set_words(rec, 1);
        
        conn_state->recognizer.reset(rec);
        getGlobalLogger()->info(conn_state->session_uuid, "Recognizer initialized");
    }
    
    if (!conn_state->recognizer) {
        getGlobalLogger()->error(conn_state->session_uuid, "Failed to create Vosk recognizer");
        return;
    }
    
    // Mark recognizer as ready BEFORE adding to connections map
    // This prevents race where audio arrives before recognizer is initialized
    {
        std::lock_guard<std::mutex> ready_lock(conn_state->processing_mutex);
        conn_state->is_ready = true;
    }
    
    // Add to connections map
    {
        std::lock_guard<std::mutex> lock(g_connections_mutex);
        g_connections[hdl] = conn_state;
        getGlobalLogger()->info(conn_state->session_uuid, "WebSocket connected (total: " + std::to_string(g_connections.size()) + ")");
    }
    
    // Send welcome message with session UUID
    json welcome = {
        {"type", "ready"},
        {"session_uuid", conn_state->session_uuid},
        {"message", "Vosk ASR ready"},
        {"sample_rate", SAMPLE_RATE},
        {"format", "16kHz Linear PCM (L16), mono, int16"},
        {"features", json::array({"partial_results", "real_time"})}
    };
    getGlobalLogger()->info(conn_state->session_uuid, "Sending welcome message");
    s->send(hdl, welcome.dump(), websocketpp::frame::opcode::text);
}

// Connection closed
void on_close(server* s, connection_hdl hdl) {
    std::shared_ptr<ConnectionState> conn_state;
    
    // Remove from connections map first
    {
        std::lock_guard<std::mutex> lock(g_connections_mutex);
        
        if (g_connections.count(hdl)) {
            conn_state = g_connections[hdl];
            g_connections.erase(hdl);
            
            // Log session end with IDs if metadata was received
            if (conn_state->metadata_received && !conn_state->call_id.empty()) {
                getGlobalLogger()->info(conn_state->session_uuid, 
                    "CallId: " + conn_state->call_id + 
                    " | FreeSWITCH UUID: " + conn_state->fs_uuid + 
                    " | Session ended");
            } else {
                getGlobalLogger()->info(conn_state->session_uuid, "WebSocket closed (total: " + std::to_string(g_connections.size()) + ")");
            }
        }
    }
    
    // Get final result (lock to ensure no audio processing is happening)
    if (conn_state && conn_state->recognizer) {
        std::lock_guard<std::mutex> processing_lock(conn_state->processing_mutex);
        
        const char* final_json = vosk_recognizer_final_result(conn_state->recognizer.get());
        auto final_obj = json::parse(final_json);
        
        if (final_obj.contains("text") && !final_obj["text"].get<std::string>().empty()) {
            std::string text = final_obj["text"];
            log_transcript(conn_state->session_uuid, text, "TRANSCRIPT_FINAL", conn_state->call_id);
            
            // Send final transcript back to FreeSWITCH for sip_caller
            // Note: We can't send via WebSocket here as connection is closed, but we can log it
            getGlobalLogger()->info(conn_state->session_uuid, 
                "Final transcript on close: " + text + " | CallId: " + conn_state->call_id);
        }
    }
}

int main() {
    // Set log level - suppress for clean output
    vosk_set_log_level(-1);
    
    // Read folder configuration from environment variables
    const char* log_folder_env = std::getenv("LOG_FOLDER");
    if (log_folder_env && strlen(log_folder_env) > 0) {
        g_log_folder = log_folder_env;
    }
    
    const char* recording_folder_env = std::getenv("RECORDING_FOLDER");
    if (recording_folder_env && strlen(recording_folder_env) > 0) {
        g_recording_folder = recording_folder_env;
    }
    
    // Create log folder if it doesn't exist
    if (!create_directory(g_log_folder)) {
        std::cerr << "[ERR] [system] Failed to create log folder: " << g_log_folder << "\n";
        return 1;
    }
    
    // Initialize global logger
    getGlobalLogger()->setLogFile("asr");
    
    // Check SAVE_AUDIO environment variable
    const char* save_audio_env = std::getenv("SAVE_AUDIO");
    if (save_audio_env && (std::string(save_audio_env) == "true" || std::string(save_audio_env) == "1")) {
        g_save_audio = true;
        
        // Create recording folder if it doesn't exist
        if (!create_directory(g_recording_folder)) {
            getGlobalLogger()->error("", "Failed to create recording folder: " + g_recording_folder);
            return 1;
        }
        
        getGlobalLogger()->info("", "Audio saving ENABLED (SAVE_AUDIO=" + std::string(save_audio_env) + ")");
        getGlobalLogger()->info("", "Recording folder: " + g_recording_folder);
    } else {
        getGlobalLogger()->info("", "Audio saving disabled (set SAVE_AUDIO=true to enable)");
    }
    
    // Load Vosk model
    const char *model_path = std::getenv("VOSK_MODEL_PATH");
    if (!model_path) {
        model_path = "/home/rammohanyadavalli/vosk/models/vosk-model-small-en-us-0.15";
    }
    
    getGlobalLogger()->info("", "Loading Vosk model from: " + std::string(model_path));
    
    g_vosk_model = vosk_model_new(model_path);
    if (!g_vosk_model) {
        getGlobalLogger()->error("", "Failed to load Vosk model from: " + std::string(model_path));
        return 1;
    }
    getGlobalLogger()->info("", "Vosk model loaded successfully");
    
    // Initialize thread pool for Vosk processing
    size_t num_threads = std::max(4u, std::thread::hardware_concurrency());
    g_thread_pool = std::make_unique<ThreadPool>(num_threads);
    getGlobalLogger()->info("", "Thread pool initialized with " + std::to_string(num_threads) + " worker threads");
    
    // Setup WebSocket server
    server ws_server;
    
    try {
        // Set logging to be less verbose
        ws_server.set_access_channels(websocketpp::log::alevel::none);
        ws_server.set_error_channels(websocketpp::log::elevel::all);
        
        // Initialize Asio
        ws_server.init_asio();
        
        // Configure for better concurrency and timeouts
        ws_server.set_reuse_addr(true);
        
        // Increase timeouts to prevent premature disconnections
        // Default is 5 seconds, increase to 60 seconds
        ws_server.set_pong_timeout(60000);  // 60 seconds
        ws_server.set_open_handshake_timeout(10000);  // 10 seconds
        ws_server.set_close_handshake_timeout(10000);  // 10 seconds
        
        // Set handlers
        ws_server.set_message_handler([&ws_server](connection_hdl hdl, message_ptr msg) {
            on_message(&ws_server, hdl, msg);
        });
        ws_server.set_open_handler([&ws_server](connection_hdl hdl) {
            on_open(&ws_server, hdl);
        });
        ws_server.set_close_handler([&ws_server](connection_hdl hdl) {
            on_close(&ws_server, hdl);
        });
        
        // Listen on port
        ws_server.listen(PORT);
        ws_server.start_accept();
        
        getGlobalLogger()->info("", "Vosk ASR WebSocket Server - MULTI-THREADED MODE");
        getGlobalLogger()->info("", "Port: " + std::to_string(PORT) + " | Format: 16kHz Linear PCM (L16), mono, int16");
        getGlobalLogger()->info("", "Worker Threads: " + std::to_string(num_threads) + " | Audio Recording: " + std::string(g_save_audio ? "ENABLED" : "DISABLED"));
        getGlobalLogger()->info("", "FreeSWITCH Config: uuid_audio_stream <uuid> start ws://172.14.3.108:9000 mixed 16k");
        
        getGlobalLogger()->info("", "Server ready, waiting for WebSocket connections");
        
        // Run the server
        ws_server.run();
    }
    catch (const std::exception& e) {
        getGlobalLogger()->error("", std::string("Server error: ") + e.what());
        g_thread_pool.reset();  // Cleanup thread pool
        vosk_model_free(g_vosk_model);
        return 1;
    }
    
    // Cleanup
    g_thread_pool.reset();  // Shutdown worker threads
    vosk_model_free(g_vosk_model);
    
    // Logger will flush/close in its destructor
    
    return 0;
}
