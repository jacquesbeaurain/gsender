#include "gs/controller/session.hpp"

#include "gs/util/strings.hpp"

#include <boost/regex.hpp>

namespace gs::controller {

Session::Session(runtime::EventLoop& loop, DeviceLink& link, SessionOptions options, ControllerHooks hooks,
                 ControllerEventSink sink)
    : loop_(loop),
      timers_(loop),
      link_(link),
      options_(options),
      hooks_(std::move(hooks)),
      sink_(std::move(sink)) {}

Session::~Session() = default;

void Session::opened() {
    if (!firmware_) {
        startDetection();
    }
}

void Session::receive(std::string_view bytes) {
    if (controller_ && controller_->ymodemListening()) {
        // An SD card upload reads bytes, not lines; what was left of a
        // line is dropped with the line reader.
        partial_.clear();
        controller_->ymodemReceive(bytes);
        return;
    }
    // ReadlineParser({ delimiter: "\n" }): a line ends at "\n"; the rest waits.
    partial_.append(bytes);
    std::size_t start = 0;
    for (std::size_t end = partial_.find('\n'); end != std::string::npos; end = partial_.find('\n', start)) {
        const std::string line = partial_.substr(start, end - start);
        start = end + 1;
        handleLine(line);
    }
    partial_.erase(0, start);
}

void Session::closed() {
    stopDetection();
    attempts_ = 0;
    partial_.clear();
    if (controller_) {
        controller_->close();
        controller_.reset();
    }
    firmware_.reset();
}

void Session::handleLine(std::string_view line) {
    if (onLine) {
        onLine(line);
    }
    if (controller_) {
        controller_->receiveLine(line);
        return;
    }
    if (firmware_) {
        return;
    }
    // Detection: the startup banner ("Grbl 1.1f ['$' for help]") or the $I
    // reply ("[FIRMWARE:grblHAL]") names the firmware. The identifying line
    // itself is not passed on - the controller did not exist yet.
    const std::string text(str::trimRight(line));
    if (text.empty()) {
        return;
    }
    static const boost::regex kGrbl(".*(grbl|fluidnc).*", boost::regex::icase);
    static const boost::regex kGrblHal(".*(grblhal).*", boost::regex::icase);
    if (boost::regex_search(text, kGrblHal)) {
        firmwareFound(protocol::Firmware::GrblHal, true);
    } else if (boost::regex_search(text, kGrbl)) {
        firmwareFound(protocol::Firmware::Grbl, true);
    }
}

// Poll $I until the reply identifies the firmware, then fall back to the
// configured default.
void Session::startDetection() {
    stopDetection();
    attempts_ = 0;
    sendQuery();
    detectTimer_ = timers_.interval(kFirmwareDetectIntervalMs, [this] {
        // The port can disappear mid-detection.
        if (!link_.isOpen()) {
            stopDetection();
            return;
        }
        if (attempts_ >= kFirmwareDetectMaxAttempts) {
            firmwareFound(options_.defaultFirmware, false);
            return;
        }
        sendQuery();
    });
}

void Session::stopDetection() {
    timers_.clear(detectTimer_);
}

void Session::sendQuery() {
    ++attempts_;
    link_.send("$I\n", SendKind::Immediate);
}

void Session::firmwareFound(protocol::Firmware firmware, bool detected) {
    // Stop first: the new controller sends its own $I.
    stopDetection();
    firmware_ = firmware;
    controller_ = std::make_unique<Controller>(loop_, link_, firmware, hooks_, sink_);
    controller_->open();
    if (onController) {
        onController(*controller_, detected);
    }
}

}  // namespace gs::controller
