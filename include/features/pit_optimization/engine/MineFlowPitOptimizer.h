#pragma once

#include "io/DatamineImporter.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct MineFlowEconomicRecord
{
    bool exists = false;
    double revenue = 0.0;
    double miningCost = 0.0;
    double processingCost = 0.0;
};

struct MineFlowPitShell
{
    double revenueFactor = 0.0;
    std::vector<std::int64_t> mineIndices;
    std::int64_t containedValue = 0;
};

struct MineFlowOptimizationInput
{
    visor::datamine::BlockModelPrototype prototype;
    std::int64_t numBlocks = 0;
    const std::vector<MineFlowEconomicRecord> *records = nullptr;
    std::vector<double> revenueFactors;
    double globalSlopeAngleDeg = 45.0;
    std::function<bool(int shellIndex, int shellCount, double revenueFactor)> beforeSolve;
};

bool BuildMineFlowRevenueFactors(
    double startRevenueFactor,
    double maxRevenueFactor,
    double revenueFactorStep,
    std::vector<double> *revenueFactors,
    std::string *errorMessage = nullptr);

bool RunMineFlowPitOptimization(
    const MineFlowOptimizationInput &input,
    std::vector<MineFlowPitShell> *shells,
    std::string *errorMessage = nullptr);
