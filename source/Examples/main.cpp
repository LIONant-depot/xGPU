// xGPU.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include "Examples.h"
#include "dependencies/xscheduler/source/xscheduler.h"

int main()
{
    xscheduler::g_System.Init();

    if constexpr (false) if (auto err = E01_Example(); err) return err;
    if constexpr (false) if (auto err = E02_Example(); err) return err;
    if constexpr (false) if (auto err = E03_Example(); err) return err;
    if constexpr (false) if (auto err = E04_Example(); err) return err;
    if constexpr (false) if (auto err = E05_Example(); err) return err;
    if constexpr (false) if (auto err = E06_Example(); err) return err;
    if constexpr (false) if (auto err = E07_Example(); err) return err;
    if constexpr (false) if (auto err = E08_Example(); err) return err;
    if constexpr (false) if (auto err = E09_Example(); err) return err;
    if constexpr (false) if (auto err = E10_Example(); err) return err;
    if constexpr (false) if (auto err = E11_Example(); err) return err;
    if constexpr (false) if (auto err = E12_Example(); err) return err;
    if constexpr (false) if (auto err = E13_Example(); err) return err;
    if constexpr (false) if (auto err = E14_Example(); err) return err;
    if constexpr (false) if (auto err = E15_Example(); err) return err;
    if constexpr (false) if (auto err = E16_Example(); err) return err;
    if constexpr (false) if (auto err = E17_Example(); err) return err;
    if constexpr (false) if (auto err = E18_Example(); err) return err;
    if constexpr (false) if (auto err = E19_Example(); err) return err;
    if constexpr (false) if (auto err = E20_Example(); err) return err;
    if constexpr (false) if (auto err = E21_Example(); err) return err;
    if constexpr (!false) if (auto err = E22_Example(); err) return err;

    return 0;
}
