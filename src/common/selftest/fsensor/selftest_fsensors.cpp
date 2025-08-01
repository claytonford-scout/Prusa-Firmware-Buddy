#include "selftest_fsensors.hpp"

namespace {

class SelftestFSensors {

public:
    using Result = SelftestFSensorsResult;

public:
    SelftestFSensors(const SelftestFSensorsParams &params)
        : params_(params) {
    }

    void run();

    inline SelftestFSensorsResult result() const {
        return result_;
    }

private:
    const SelftestFSensorsParams params_;
    Result result_ = Result::success;
};

void SelftestFSensors::run() {
    // TODO
}

} // namespace

SelftestFSensorsResult run_selftest_fsensors(const SelftestFSensorsParams &params) {
    SelftestFSensors test { params };
    test.run();
    return test.result();
}
