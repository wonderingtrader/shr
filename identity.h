#pragma once
#include <string>

class Identity {
public:
    static Identity& instance();
    bool        install();
    bool        load();
    std::string id() const { return id_; }
    bool        is_installed() const;

private:
    Identity() = default;
    std::string id_;
};
