#pragma once

#include <QString>
#include <QVector>

namespace pp {

struct MultipleTestingHypothesis {
    QString hypothesisId;
    double rawPValue = 0.0;
    double holmAdjustedPValue = 0.0;
    double bhAdjustedPValue = 0.0;
    bool holmRejected = false;
    bool bhRejected = false;
};

struct MultipleTestingSummary {
    bool available = false;
    double alpha = 0.05;
    QString candidateSetSha256;
    QVector<MultipleTestingHypothesis> hypotheses;
};

class MultipleTestingCorrection final {
public:
    static MultipleTestingSummary adjust(
        const QVector<MultipleTestingHypothesis> &hypotheses,
        double alpha = 0.05, QString *error = nullptr);
};

} // namespace pp
