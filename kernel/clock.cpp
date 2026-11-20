#include "clock.hpp"

namespace trist::clock {

static DateTime current;
static volatile uint16_t* const video = reinterpret_cast<volatile uint16_t*>(0xb8000);

static uint8_t read_cmos(uint8_t address) {
    asm volatile("outb %0, %1" : : "a"(address), "Nd"(static_cast<uint16_t>(0x70)));
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(static_cast<uint16_t>(0x71)));
    return value;
}

static void write_cmos(uint8_t address, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(static_cast<uint8_t>(0x80 | address)), "Nd"(static_cast<uint16_t>(0x70)));
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(static_cast<uint16_t>(0x71)));
}

static uint8_t bcd_to_binary(uint8_t value) {
    return static_cast<uint8_t>((value & 0x0f) + ((value >> 4) * 10));
}

static uint8_t binary_to_bcd(uint8_t value) {
    return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

static bool two_digits(const char* text, uint32_t offset, uint8_t* value) {
    if (text[offset] < '0' || text[offset] > '9' || text[offset + 1] < '0' || text[offset + 1] > '9') return false;
    *value = static_cast<uint8_t>((text[offset] - '0') * 10 + text[offset + 1] - '0');
    return true;
}

static void read_rtc() {
    current.second = bcd_to_binary(read_cmos(0x00));
    current.minute = bcd_to_binary(read_cmos(0x02));
    current.hour = bcd_to_binary(read_cmos(0x04));
    current.day = bcd_to_binary(read_cmos(0x07));
    current.month = bcd_to_binary(read_cmos(0x08));
    current.year = bcd_to_binary(read_cmos(0x09));
}

static uint8_t digit(uint8_t value, uint8_t place) {
    return static_cast<uint8_t>((value / place) % 10);
}

static uint16_t cell(char value) {
    return static_cast<uint16_t>(0x1f00 | static_cast<uint8_t>(value));
}

void initialize() {
    read_rtc();
    update_screen();
}

bool set_from_text(const char* text) {
    DateTime requested;
    if (!two_digits(text, 0, &requested.hour) || text[2] != ':' ||
        !two_digits(text, 3, &requested.minute) || text[5] != ':' ||
        !two_digits(text, 6, &requested.second) || text[8] != ' ' ||
        !two_digits(text, 9, &requested.day) || text[11] != '/' ||
        !two_digits(text, 12, &requested.month) || text[14] != '/' ||
        !two_digits(text, 15, &requested.year)) return false;
    if (requested.hour > 23 || requested.minute > 59 || requested.second > 59 ||
        requested.day == 0 || requested.day > 31 || requested.month == 0 || requested.month > 12) return false;
    write_cmos(0x00, binary_to_bcd(requested.second));
    write_cmos(0x02, binary_to_bcd(requested.minute));
    write_cmos(0x04, binary_to_bcd(requested.hour));
    write_cmos(0x07, binary_to_bcd(requested.day));
    write_cmos(0x08, binary_to_bcd(requested.month));
    write_cmos(0x09, binary_to_bcd(requested.year));
    current = requested;
    update_screen();
    return true;
}

void update_screen() {
    read_rtc();
    const char* label = "CLK ";
    for (uint32_t i = 0; label[i] != 0; ++i) video[68 + i] = cell(label[i]);
    video[73] = cell(static_cast<char>('0' + digit(current.hour, 10)));
    video[74] = cell(static_cast<char>('0' + digit(current.hour, 1)));
    video[75] = cell(':');
    video[76] = cell(static_cast<char>('0' + digit(current.minute, 10)));
    video[77] = cell(static_cast<char>('0' + digit(current.minute, 1)));
    video[78] = cell(':');
    video[79] = cell(static_cast<char>('0' + digit(current.second, 10)));
}

const DateTime& now() {
    return current;
}

}
