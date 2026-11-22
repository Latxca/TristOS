#include "media.hpp"
#include "kernel.hpp"

namespace trist::media {

void browser() {
    console_write_line("webbrowser.texe: browser service ready");
    console_write_line("Networking and HTML rendering drivers are not enabled yet.");
}

void calculator() {
    console_write_line("calculator.texe: calculator service ready");
    console_write_line("Use the command prompt for system commands; arithmetic UI is pending.");
}

void photos() {
    console_write_line("photos.texe: photo viewer ready for framebuffer image backends");
}

void videos() {
    console_write_line("videos.texe: video service ready for framebuffer playback backends");
}

}
