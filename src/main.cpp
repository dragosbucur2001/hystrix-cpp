#include <chrono>
#include <exception>
#include <expected>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <variant>

using namespace std::literals;

class OpenCircuitException : public std::exception {
  const std::string _key;

public:
  OpenCircuitException(const std::string_view &key) : _key(key) {}
  const char *what() const throw() { return _key.c_str(); }
};

class CircuitBreaker {
private:
  enum class CircuitBreakerState { OPEN, CLOSED, SEMIOPEN };

  struct CircuitBreakerMetadata {
    CircuitBreakerState state;
    int max_retries;
    int retries;
    std::chrono::seconds wait_time;
    std::chrono::time_point<std::chrono::system_clock> last_triggered;
  };

  std::unordered_map<std::string, CircuitBreakerMetadata> _command_to_metadata;

public:
  template <typename T>
  T run(std::string_view key, std::function<T(void) noexcept(false)> f,
        std::function<T(void) noexcept(false)> fallback = nullptr) {
    CircuitBreakerMetadata x{.max_retries = 2, .wait_time = 10s};
    const auto [it, inserted] =
        _command_to_metadata.insert({std::string(key), x});
    auto &metadata = it->second;

    if (metadata.state == CircuitBreakerState::CLOSED) {
      const auto _now = std::chrono::system_clock::now();

      if (_now - metadata.last_triggered < metadata.wait_time) {
        if (fallback)
          return fallback();

        throw OpenCircuitException(key);
      }

      metadata.last_triggered = _now;
      metadata.state = CircuitBreakerState::SEMIOPEN;
    }

    try {
      const auto x = f();
      if (metadata.state == CircuitBreakerState::SEMIOPEN) {
        metadata.state = CircuitBreakerState::OPEN;
        metadata.retries = 0;
      }

      return x;
    } catch (...) {
      metadata.retries++;

      if (metadata.retries > metadata.max_retries) {
        std::cout << "Max Failures triggered" << std::endl;
        metadata.last_triggered = std::chrono::system_clock::now();
        metadata.state = CircuitBreakerState::CLOSED;
      }

      if (fallback)
        return fallback();

      throw;
    }
  }
};

int main() {
  CircuitBreaker cb;
  std::cout << "Testing, format: <key> <throws: y/n/f>\n";

  std::string key;
  std::string throws;
  while (std::cin >> key >> throws) {
    if (throws == "y") {
      try {
        const auto x = cb.run<int>(
            key, []() -> int { throw std::runtime_error("failure inside f"); });

        std::cout << x << std::endl;
      } catch (const OpenCircuitException &e) {
        std::cout << "Ciruit is open" << std::endl;
      } catch (...) {
        std::cout << "Fuck up at no fallback: " << std::endl;
      }
    } else if (throws == "n") {
      try {
        const auto x = cb.run<int>(key, []() -> int { return 1; });

        std::cout << x << std::endl;
      } catch (const OpenCircuitException &e) {
        std::cout << "Ciruit is open" << std::endl;
      } catch (...) {
        std::cout << "Fuck up at fallback: " << std::endl;
      }
    } else {
      try {
        const auto x = cb.run<int>(
            key, []() -> int { throw std::runtime_error("failure inside f"); },
            []() -> int { return -1; });

        std::cout << x << std::endl;
      } catch (const OpenCircuitException &e) {
        std::cout << "Ciruit is open" << std::endl;
      } catch (...) {
        std::cout << "Fuck up at fallback: " << std::endl;
      }
    }
  }
  return 0;
}
