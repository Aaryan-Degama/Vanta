#pragma once

#include <string>
#include <vector>
#include "sqlite3.h"
#include <cstdint>


class Entity {
    private:
    bool is_named = false;

    public:
    bool is_nameed();
    bool name_it(const std::string& name);
    void set_relation(const)
}