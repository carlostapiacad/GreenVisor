#include "features/pit_optimization/engine/MineFlowPitOptimizer.h"

#include "mineflow.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace
{
bool SetError(std::string *errorMessage, const std::string &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}

std::int64_t ScaledMineFlowValue(double value)
{
    constexpr double scale = 100.0;
    const double scaled = std::round(value * scale);
    const double lo = static_cast<double>(std::numeric_limits<std::int64_t>::min() / 4);
    const double hi = static_cast<double>(std::numeric_limits<std::int64_t>::max() / 4);
    return static_cast<std::int64_t>(std::clamp(scaled, lo, hi));
}
}

bool BuildMineFlowRevenueFactors(
    double startRevenueFactor,
    double maxRevenueFactor,
    double revenueFactorStep,
    std::vector<double> *revenueFactors,
    std::string *errorMessage)
{
    if (!revenueFactors) {
        return SetError(errorMessage, "Revenue factor output is not available.");
    }
    revenueFactors->clear();
    if (revenueFactorStep <= 0.0 || maxRevenueFactor < startRevenueFactor) {
        return SetError(errorMessage, "Invalid revenue factor range.");
    }
    for (double rf = startRevenueFactor; rf <= maxRevenueFactor + revenueFactorStep * 0.25; rf += revenueFactorStep) {
        revenueFactors->push_back(std::min(rf, maxRevenueFactor));
        if (revenueFactors->size() > 1000) {
            return SetError(errorMessage, "The revenue factor range creates too many pit shells.");
        }
    }
    if (revenueFactors->empty()) {
        return SetError(errorMessage, "The revenue factor range is empty.");
    }
    return true;
}

bool RunMineFlowPitOptimization(
    const MineFlowOptimizationInput &input,
    std::vector<MineFlowPitShell> *shells,
    std::string *errorMessage)
{
    if (!shells) {
        return SetError(errorMessage, "Pit shell output is not available.");
    }
    shells->clear();
    if (!input.records) {
        return SetError(errorMessage, "Economic records are not available.");
    }
    if (input.numBlocks <= 0 || static_cast<std::size_t>(input.numBlocks) > input.records->size()) {
        return SetError(errorMessage, "Economic grid dimensions are invalid.");
    }
    if (input.revenueFactors.empty()) {
        return SetError(errorMessage, "No revenue factors were provided.");
    }

    namespace mf = mvd::mineflow;
    const auto &prototype = input.prototype;
    mf::BlockDefinition blockDef(
        prototype.nx,
        prototype.ny,
        prototype.nz,
        prototype.xOrigin,
        prototype.yOrigin,
        prototype.zOrigin,
        prototype.parentX,
        prototype.parentY,
        prototype.parentZ);
    const double slopeRad = static_cast<double>(mf::ToRadians(static_cast<long double>(input.globalSlopeAngleDeg)));
    mf::SlopeDefinition slopeDefinition = mf::SlopeDefinition::Constant(slopeRad);
    mf::PrecedencePattern pattern = mf::PrecedencePattern::MinSearch(blockDef, slopeDefinition, blockDef.NumZ);
    auto precedence = std::make_shared<mf::Regular3DBlockModelPatternPrecedence>(blockDef, pattern);

    shells->reserve(input.revenueFactors.size());
    for (int shellIndex = 0; shellIndex < static_cast<int>(input.revenueFactors.size()); ++shellIndex) {
        const double rf = input.revenueFactors[static_cast<std::size_t>(shellIndex)];
        if (input.beforeSolve && !input.beforeSolve(shellIndex, static_cast<int>(input.revenueFactors.size()), rf)) {
            return SetError(errorMessage, "MineFlow optimization was cancelled.");
        }

        auto values = std::make_shared<mf::VecBlockValues>(static_cast<mf::IndexType>(input.numBlocks));
        for (std::int64_t idx = 0; idx < input.numBlocks; ++idx) {
            const MineFlowEconomicRecord &record = (*input.records)[static_cast<std::size_t>(idx)];
            const double value = record.exists
                ? (rf * record.revenue - record.miningCost - record.processingCost)
                : 0.0;
            values->SetBlockValueSI(static_cast<mf::IndexType>(idx), ScaledMineFlowValue(value));
        }

        mf::PseudoSolverSolveInfo solveInfo;
        mf::PseudoSolver solver(precedence, values);
        solver.Solve(&solveInfo);

        MineFlowPitShell shell;
        shell.revenueFactor = rf;
        shell.containedValue = solveInfo.ContainedValue;
        shell.mineIndices.reserve(static_cast<std::size_t>(solveInfo.NumContainedNodes));
        for (std::int64_t idx = 0; idx < input.numBlocks; ++idx) {
            const MineFlowEconomicRecord &record = (*input.records)[static_cast<std::size_t>(idx)];
            if (!record.exists) {
                continue;
            }
            if (solver.InMinimumCut(static_cast<mf::IndexType>(idx))) {
                shell.mineIndices.push_back(idx);
            }
        }
        shells->push_back(std::move(shell));
    }

    return true;
}
