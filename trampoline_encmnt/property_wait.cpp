#include <chrono>
#include <string>

namespace android {
namespace base {

    bool WaitForProperty(const std::string& key, const std::string& expected_value,
                     std::chrono::milliseconds relative_timeout) {
        return true;
    }

}
}
