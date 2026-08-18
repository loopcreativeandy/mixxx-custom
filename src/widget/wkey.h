#pragma once

#include <memory>
#include <vector>

#include "widget/wlabel.h"
#include "control/controlproxy.h"

class WKey : public WLabel  {
    Q_OBJECT
  public:
    explicit WKey(const QString& group, QWidget* pParent = nullptr);

    void onConnectedControlChanged(double dParameter, double dValue) override;
    void setup(const QDomNode& node, const SkinContext& context) override;

  private slots:
    void setValue(double dValue);
    void keyNotationChanged(double dValue);
    void setCents();
    void updateKeyClash();

  private:
    QString m_group;
    double m_dOldValue;
    bool m_displayCents;
    bool m_displayKey;
    // Andy: append the integer semitone offset from the file key (" +2"/" -1")
    bool m_displayOffset;
    bool m_keyClash;
    ControlProxy m_keyNotation;
    ControlProxy m_engineKeyDistance;
    ControlProxy m_pitch;
    // Zouk mode (Andy): turn red when playing decks clash on the Camelot wheel
    ControlProxy m_zoukMode;
    ControlProxy m_playSelf;
    struct OtherDeck {
        QString group;
        std::unique_ptr<ControlProxy> pKey;
        std::unique_ptr<ControlProxy> pPlay;
    };
    std::vector<OtherDeck> m_otherDecks;
};
