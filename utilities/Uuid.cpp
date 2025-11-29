#include "Uuid.h"
#include <random>
#include <sstream>

namespace util {

static char toHex(int value) {
    if (value >= 0 && value <= 9) return static_cast<char>('0' + value);
    if (value >= 10 && value <= 15) return static_cast<char>('a' + (value - 10));
    return '0';
}

std::string generateUuidV4() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    static thread_local std::uniform_int_distribution<> dis(0, 15);
    static thread_local std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    // 8 hex
    for (int i = 0; i < 8; ++i) ss << toHex(dis(gen));
    ss << '-';
    // 4 hex
    for (int i = 0; i < 4; ++i) ss << toHex(dis(gen));
    ss << '-';
    // 4xxx (v4)
    ss << '4';
    for (int i = 0; i < 3; ++i) ss << toHex(dis(gen));
    ss << '-';
    // yxxx (variant)
    ss << toHex(dis2(gen));
    for (int i = 0; i < 3; ++i) ss << toHex(dis(gen));
    ss << '-';
    // 12 hex
    for (int i = 0; i < 12; ++i) ss << toHex(dis(gen));
    return ss.str();
}

}


