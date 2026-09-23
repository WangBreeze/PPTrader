#pragma once

// 仅 BUILD_TESTING 构建：真实 Controller / QML 的隔离契约回归。
int runWizardContractProbe();
int runPaperStrategyContractProbe();
int runPaperFlowAcceptance();
