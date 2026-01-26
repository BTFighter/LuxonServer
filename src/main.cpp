#include "luxon/server/server_manager.hpp"
#include "platform.hpp"

#include <optional>
#include <csignal>

int main() {
    Platform P;
    server::ServerManager("config.yml").run();
}
