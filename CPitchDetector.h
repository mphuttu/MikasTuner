#pragma once
#include <vector>
#include <deque>

class CPitchDetector
{
public:
    CPitchDetector(int sampleRate);
    ~CPitchDetector();

    void ProcessBuffer(const short* samples, int count);
    double GetFrequency() const { return m_smoothedFrequency; }
    double GetRawFrequency() const { return m_frequency; }
    double GetConfidence() const { return m_confidence; }
    bool IsValidSignal() const { return m_isValidSignal; }

    // Kitaran kielet ja niiden taajuudet (standardiviritys)
    struct GuitarString {
        const wchar_t* name;
        double frequency;
    };

    static const GuitarString GUITAR_STRINGS[6];
    const GuitarString* GetNearestString(); // Palauttaa lähimmän kielen (päivittää lukitusta)
    int GetLockedStringIndex() const { return m_lockedStringIndex; }
    double GetCentsOff() const; // Kuinka monta senttiä ohi tavoitteesta

    // Asetukset
    void SetNoiseThreshold(double threshold) { m_noiseThreshold = threshold; }
    void SetMinConfidence(double confidence) { m_minConfidence = confidence; }
    void SetSmoothingFactor(double factor) { m_smoothingFactor = factor; }
    void Reset(); // Nollaa tasoitushistoria

private:
    double DetectPitch(const short* samples, int count);
    void RemoveDC(const short* in, double* out, int count);
    double CalculateRMS(const short* samples, int count);
    double ParabolicInterpolation(const std::vector<double>& data, int peakIndex);
    void UpdateSmoothedFrequency(double newFrequency);
    bool IsFrequencyStable(double newFrequency) const;

    int m_sampleRate;
    double m_frequency;           // Raaka taajuus
    double m_smoothedFrequency;   // Tasoitettu taajuus
    double m_confidence;
    bool m_isValidSignal;

    // Aikaperusteinen tasoitus
    std::deque<double> m_frequencyHistory;
    static constexpr int HISTORY_SIZE = 5;        // Montako näytettä muistetaan
    double m_smoothingFactor;                      // Eksponentiaalisen tasoituksen kerroin (0-1)
    double m_lastValidFrequency;
    int m_stableCount;                            // Kuinka monta kertaa sama taajuus peräkkäin

    // Parametrit
    double m_noiseThreshold;      // RMS-kynnys kohinalle
    double m_minConfidence;       // Minimi luotettavuus
    static constexpr double MIN_GUITAR_FREQ = 75.0;       // E2 ~82 Hz, hieman alle
    static constexpr double MAX_GUITAR_FREQ = 350.0;      // E4 ~330 Hz, hieman yli
    static constexpr double FREQUENCY_JUMP_THRESHOLD = 50.0; // Hz, suuremmat hypyt nollaavat historian
    static constexpr int STABLE_COUNT_THRESHOLD = 3;      // Montako kertaa pitää toistua ennen hyväksyntää

    // Kielilukitus (string lock) - estää hyppimistä kielten välillä
    int m_lockedStringIndex;                              // Lukittu kieli (-1 = ei lukitusta)
    int m_stringLockCount;                                // Montako kertaa sama kieli havaittu peräkkäin
    int m_lastDetectedStringIndex;                        // Edellinen havaittu kieli (lukitusta varten)
    static constexpr int STRING_LOCK_THRESHOLD = 5;       // Montako havaintoa ennen lukitusta
    static constexpr double STRING_UNLOCK_CENTS = 55.0;   // Kuinka monta senttiä ennen lukituksen vapautusta
    static constexpr int STRING_UNLOCK_COUNT = 4;         // Montako kertaa pitää olla unlock-alueen ulkopuolella
};
