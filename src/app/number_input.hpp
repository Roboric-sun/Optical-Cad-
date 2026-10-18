#pragma once
#include <QDoubleSpinBox>
#include <QLocale>
#include <cmath>

// UI decimals always accept either separator; commas are never thousands separators.
inline double uiNumber(QString text, bool* ok = nullptr) {
    bool valid = false;
    const double value = QLocale::c().toDouble(text.trimmed().replace(',', '.'), &valid);
    valid = valid && std::isfinite(value);
    if (ok) *ok = valid;
    return valid ? value : 0;
}
class DecimalSpinBox : public QDoubleSpinBox {
  public:
    explicit DecimalSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {
        setLocale(QLocale::c()); setGroupSeparatorShown(false);
    }
  protected:
    QValidator::State validate(QString& text, int& position) const override {
        QString normalized = text; normalized.replace(',', '.');
        return QDoubleSpinBox::validate(normalized, position);
    }
    double valueFromText(const QString& text) const override {
        QString value = text;
        if (!prefix().isEmpty() && value.startsWith(prefix())) value.remove(0, prefix().size());
        if (!suffix().isEmpty() && value.endsWith(suffix())) value.chop(suffix().size());
        return uiNumber(value);
    }
};
