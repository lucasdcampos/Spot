#include <spot.h>

#include <memory>

int main() {
    Spot::Log::AddSink(std::make_shared<Spot::FileSink>("sandbox.log"));
    Spot::Log::Info("Spot {}", Spot::GetVersionString());
    return 0;
}
