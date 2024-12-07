#include <string>
#include <algorithm>
#include <cctype>

class Utils {
public:
    // Prevent instantiation
    Utils() = delete;

    // Static utility methods
    static std::string toLowerCase(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return result;
    }

    static std::string toUpperCase(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char c) { return std::toupper(c); });
        return result;
    }

    static bool isNumeric(const std::string& str) {
        return !str.empty() &&
            std::all_of(str.begin(), str.end(), [](unsigned char c) { return std::isdigit(c); });
    }
};

#endif // UTILS_H
