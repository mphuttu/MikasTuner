#include "pch.h"
#include "CPitchDetector.h"
#include <cmath>

#include <numeric>
#define NOMINMAX
#include <Windows.h>
#undef min 
#undef max
#include <algorithm>


// Guitar standard tuning (E2, A2, D3, G3, B3, E4)
const CPitchDetector::GuitarString CPitchDetector::GUITAR_STRINGS[6] = {
    { L"E low", 82.41 },
    { L"A", 110.00 },
    { L"D", 146.83 },
    { L"G", 196.00 },
    { L"B", 246.94 },
    { L"E high", 329.63 }
};

CPitchDetector::CPitchDetector(int sampleRate)
    : m_sampleRate(sampleRate)
    , m_frequency(0.0)
    , m_smoothedFrequency(0.0)
    , m_confidence(0.0)
    , m_isValidSignal(false)
    , m_smoothingFactor(0.3)      // Pienempi arvo = enemmän tasoitusta
    , m_lastValidFrequency(0.0)
    , m_stableCount(0)
    , m_noiseThreshold(500.0)
    , m_minConfidence(0.3)
    , m_lockedStringIndex(-1)
    , m_stringLockCount(0)
    , m_lastDetectedStringIndex(-1)
{
}

CPitchDetector::~CPitchDetector()
{
}

void CPitchDetector::Reset()
{
    m_frequencyHistory.clear();
    m_smoothedFrequency = 0.0;
    m_lastValidFrequency = 0.0;
    m_stableCount = 0;
    m_isValidSignal = false;
    m_lockedStringIndex = -1;
    m_stringLockCount = 0;
    m_lastDetectedStringIndex = -1;
}

double CPitchDetector::CalculateRMS(const short* samples, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
    {
        sum += static_cast<double>(samples[i]) * samples[i];
    }
    return std::sqrt(sum / count);
}

void CPitchDetector::RemoveDC(const short* in, double* out, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
    {
        sum += in[i];
    }
    double mean = sum / count;
    for (int i = 0; i < count; ++i)
    {
        out[i] = in[i] - mean;
    }
}

// Parabolinen interpolointi tarkemman taajuuden löytämiseksi
double CPitchDetector::ParabolicInterpolation(const std::vector<double>& data, int peakIndex)
{
    if (peakIndex <= 0 || peakIndex >= static_cast<int>(data.size()) - 1)
        return static_cast<double>(peakIndex);

    double y0 = data[peakIndex - 1];
    double y1 = data[peakIndex];
    double y2 = data[peakIndex + 1];

    double denominator = y0 - 2.0 * y1 + y2;
    if (std::abs(denominator) < 1e-10)
        return static_cast<double>(peakIndex);

    double d = (y0 - y2) / (2.0 * denominator);
    return peakIndex + d;
}

bool CPitchDetector::IsFrequencyStable(double newFrequency) const
{
    if (m_lastValidFrequency <= 0.0)
        return true; // Ensimmäinen mittaus, hyväksytään

    // Käytä suhteellista vertailua (prosenttiero) absoluuttisen sijaan
    // Tämä toimii paremmin eri taajuusalueilla
    double diff = std::abs(newFrequency - m_lastValidFrequency);
    double avgFreq = (newFrequency + m_lastValidFrequency) / 2.0;
    double percentDiff = (diff / avgFreq) * 100.0;

    // Sallitaan ~5% vaihtelu saman kielen sisällä (vastaa ~1 puolisävelaskelta)
    // Esim. 247 Hz (B) +/- 5% = 235-259 Hz
    return percentDiff < 5.0;
}

void CPitchDetector::UpdateSmoothedFrequency(double newFrequency)
{
    // Tunnista kielenvaihto vertaamalla lähintä kieltä
    int newStringIndex = -1;
    int oldStringIndex = -1;
    double minDiff = 1e9;

    // Etsi lähin kieli uudelle taajuudelle
    for (int i = 0; i < 6; i++)
    {
        double diff = fabs(newFrequency - GUITAR_STRINGS[i].frequency);
        if (diff < minDiff)
        {
            minDiff = diff;
            newStringIndex = i;
        }
    }

    // Etsi lähin kieli vanhalle taajuudelle (jos on)
    if (m_smoothedFrequency > 0.0)
    {
        minDiff = 1e9;
        for (int i = 0; i < 6; i++)
        {
            double diff = fabs(m_smoothedFrequency - GUITAR_STRINGS[i].frequency);
            if (diff < minDiff)
            {
                minDiff = diff;
                oldStringIndex = i;
            }
        }
    }

    // Jos kieli vaihtui, nollaa historia ja käytä uutta taajuutta suoraan
    if (oldStringIndex >= 0 && newStringIndex != oldStringIndex)
    {
        ATLTRACE(_T("String changed: %s -> %s (%.1f Hz)\n"),
            GUITAR_STRINGS[oldStringIndex].name,
            GUITAR_STRINGS[newStringIndex].name,
            newFrequency);

        m_frequencyHistory.clear();
        m_frequencyHistory.push_back(newFrequency);
        m_smoothedFrequency = newFrequency;
        m_lastValidFrequency = newFrequency;
        m_stableCount = STABLE_COUNT_THRESHOLD; // Salli välitön näyttö
        return;
    }

    // Lisätään historiaan
    m_frequencyHistory.push_back(newFrequency);
    if (m_frequencyHistory.size() > HISTORY_SIZE)
    {
        m_frequencyHistory.pop_front();
    }

    // Tarkista onko taajuus vakaa saman kielen sisällä
    if (!IsFrequencyStable(newFrequency))
    {
        m_stableCount = 0;
        // Päivitä silti tasoitettua taajuutta, mutta hitaammin
        if (m_smoothedFrequency > 0.0)
        {
            m_smoothedFrequency = 0.3 * newFrequency + 0.7 * m_smoothedFrequency;
        }
        return;
    }

    m_stableCount++;

    // Vaadi muutama peräkkäinen vakaa mittaus ennen täyttä päivitystä
    if (m_stableCount < STABLE_COUNT_THRESHOLD)
    {
        // Päivitä silti, mutta hitaasti
        if (m_smoothedFrequency <= 0.0)
        {
            m_smoothedFrequency = newFrequency;
        }
        else
        {
            m_smoothedFrequency = 0.5 * newFrequency + 0.5 * m_smoothedFrequency;
        }
        return;
    }

    // Lasketaan mediaani poikkeavien arvojen suodattamiseksi
    if (m_frequencyHistory.size() >= 3)
    {
        std::vector<double> sorted(m_frequencyHistory.begin(), m_frequencyHistory.end());
        std::sort(sorted.begin(), sorted.end());
        size_t mid = sorted.size() / 2;
        double medianFreq = sorted[mid];

        // Käytetään eksponentiaalista tasoitusta mediaanin kanssa
        if (m_smoothedFrequency <= 0.0)
        {
            m_smoothedFrequency = medianFreq;
        }
        else
        {
            m_smoothedFrequency = m_smoothingFactor * medianFreq + 
                                  (1.0 - m_smoothingFactor) * m_smoothedFrequency;
        }
    }
    else
    {
        // Liian vähän historiaa, käytetään suoraan uutta arvoa
        if (m_smoothedFrequency <= 0.0)
        {
            m_smoothedFrequency = newFrequency;
        }
        else
        {
            m_smoothedFrequency = m_smoothingFactor * newFrequency + 
                                  (1.0 - m_smoothingFactor) * m_smoothedFrequency;
        }
    }

    m_lastValidFrequency = newFrequency;
}

void CPitchDetector::ProcessBuffer(const short* samples, int count)
{
    m_isValidSignal = false;
    m_confidence = 0.0;

    if (count < 256)
    {
        m_frequency = 0.0;
        return;
    }

    // 1. Tarkista signaalin voimakkuus (kohinaportti)
    double rms = CalculateRMS(samples, count);
    if (rms < m_noiseThreshold)
    {
        m_frequency = 0.0;
        // Nollaa tasoitus jos signaali katoaa pitkäksi aikaa
        if (m_frequencyHistory.size() > 0)
        {
            m_stableCount = 0;
        }
        return; // Liian hiljainen signaali
    }

    m_frequency = DetectPitch(samples, count);

    // Tarkista että taajuus on kitaran alueella
    if (m_frequency < MIN_GUITAR_FREQ || m_frequency > MAX_GUITAR_FREQ)
    {
        m_frequency = 0.0;
        m_confidence = 0.0;
        return;
    }

    // Tarkista luotettavuus
    if (m_confidence < m_minConfidence)
    {
        m_frequency = 0.0;
        return;
    }

    // Päivitä tasoitettu taajuus
    UpdateSmoothedFrequency(m_frequency);

    m_isValidSignal = true;
}

double CPitchDetector::DetectPitch(const short* samples, int count)
{
    // 1. Poistetaan DC-komponentti
    std::vector<double> centeredSamples(count);
    RemoveDC(samples, centeredSamples.data(), count);

    // 2. Normalisoidaan signaali
    double maxVal = 0.0;
    for (int i = 0; i < count; ++i)
    {
        maxVal = std::max(maxVal, std::abs(centeredSamples[i]));
    }
    if (maxVal > 0)
    {
        for (int i = 0; i < count; ++i)
        {
            centeredSamples[i] /= maxVal;
        }
    }

    // 3. Autokorrelaatio
    int maxLag = count / 2;
    std::vector<double> autocorrelation(maxLag);

    double autocorrAtZero = 0.0;
    for (int i = 0; i < count; ++i)
    {
        autocorrAtZero += centeredSamples[i] * centeredSamples[i];
    }

    if (autocorrAtZero < 1e-10)
    {
        m_confidence = 0.0;
        return 0.0;
    }

    for (int lag = 0; lag < maxLag; ++lag)
    {
        double sum = 0.0;
        for (int i = 0; i < count - lag; ++i)
        {
            sum += centeredSamples[i] * centeredSamples[i + lag];
        }
        autocorrelation[lag] = sum / autocorrAtZero;
    }

    // 4. Kitaran taajuusalue
    int minLag = m_sampleRate / static_cast<int>(MAX_GUITAR_FREQ); // 350 Hz -> lag ~126
    int maxLagSearch = m_sampleRate / static_cast<int>(MIN_GUITAR_FREQ); // 75 Hz -> lag ~588

    if (maxLagSearch > maxLag)
        maxLagSearch = maxLag;

    if (minLag < 1)
        minLag = 1;

    // PARANNETTU ALGORITMI: Etsi ensimmäinen piikki autokorrelaation laskun jälkeen
    // Tämä löytää perustaajuuden, ei harmonisia

    // Vaihe 1: Etsi kohta jossa autokorrelaatio laskee alle 0.5 (ensimmäinen "laakso")
    int firstValley = minLag;
    for (int lag = 1; lag < maxLagSearch; ++lag)
    {
        if (autocorrelation[lag] < 0.5)
        {
            firstValley = lag;
            break;
        }
    }

    // Vaihe 2: Etsi ENSIMMÄINEN merkittävä piikki laakson jälkeen
    int peakLag = -1;
    double peakValue = 0.0;

    for (int lag = firstValley; lag < maxLagSearch - 1; ++lag)
    {
        // Onko paikallinen maksimi?
        if (autocorrelation[lag] > autocorrelation[lag - 1] &&
            autocorrelation[lag] > autocorrelation[lag + 1])
        {
            // Onko tarpeeksi vahva? (vähintään 0.3)
            if (autocorrelation[lag] > 0.3)
            {
                peakLag = lag;
                peakValue = autocorrelation[lag];
                break; // OTA ENSIMMÄINEN vahva piikki - tämä on perustaajuus!
            }
        }
    }

    // Jos ei löytynyt, kokeile löysemmällä kynnyksellä
    if (peakLag <= 0)
    {
        for (int lag = firstValley; lag < maxLagSearch - 1; ++lag)
        {
            if (autocorrelation[lag] > autocorrelation[lag - 1] &&
                autocorrelation[lag] > autocorrelation[lag + 1])
            {
                if (autocorrelation[lag] > 0.2)
                {
                    peakLag = lag;
                    peakValue = autocorrelation[lag];
                    break;
                }
            }
        }
    }

    if (peakLag <= 0)
    {
        m_confidence = 0.0;
        return 0.0;
    }

    // 5. Tallenna luotettavuus
    m_confidence = peakValue;

    // 6. Parabolinen interpolointi
    double refinedLag = ParabolicInterpolation(autocorrelation, peakLag);

    // 7. Lasketaan taajuus
    double frequency = static_cast<double>(m_sampleRate) / refinedLag;

    // 8. Oktaavivirhe-korjaus matalille taajuuksille
    // Matalien kielten harmoniset (oktaavit) voivat dominoida signaalia
    // E low = 82 Hz, oktaavi = 164 Hz
    // A = 110 Hz, oktaavi = 220 Hz  
    // D = 147 Hz, oktaavi = 294 Hz

    // Tarkista onko havaittu taajuus mahdollinen oktaavi matalasta kielestä
    for (int i = 0; i < 3; ++i)  // E low, A, D
    {
        double targetFreq = GUITAR_STRINGS[i].frequency;
        double octaveFreq = targetFreq * 2.0;

        // Onko havaittu taajuus lähellä tämän kielen oktaavia?
        double centsDiff = std::abs(1200.0 * std::log2(frequency / octaveFreq));

        if (centsDiff < 60.0)  // Lähellä oktaavia
        {
            // Tarkista onko autokorrelaatiossa vahvempi piikki kaksinkertaisella lag:lla
            // (kaksinkertainen lag = puolikas taajuus = perustaajuus)
            int doubleLag = peakLag * 2;
            if (doubleLag < maxLagSearch)
            {
                double doubleLagValue = autocorrelation[doubleLag];

                // Jos kaksinkertaisella lag:lla on merkittävä piikki, 
                // kyseessä on todennäköisesti oktaavivirhe
                if (doubleLagValue > 0.12)
                {
                    double correctedFreq = frequency / 2.0;
                    TRACE(_T("Octave correction: %.1f Hz -> %.1f Hz (string %s, doubleLag=%.2f)\n"), 
                        frequency, correctedFreq, GUITAR_STRINGS[i].name, doubleLagValue);
                    frequency = correctedFreq;
                    break;
                }
            }
        }
    }

    // Debug: tulosta havaittu taajuus
    TRACE(_T("DetectPitch: lag=%d, freq=%.1f Hz, conf=%.2f\n"), peakLag, frequency, peakValue);

    return frequency;
}

const CPitchDetector::GuitarString* CPitchDetector::GetNearestString()
{
    if (m_smoothedFrequency <= 0 || !m_isValidSignal)
    {
        // Signaali katosi - nollaa lukitus hitaasti
        if (m_stringLockCount > 0)
            m_stringLockCount--;
        if (m_stringLockCount == 0)
        {
            m_lockedStringIndex = -1;
            m_lastDetectedStringIndex = -1;
        }
        return nullptr;
    }

    // Etsi lähin kieli taajuuden perusteella
    // Käytä senttejä Hz:n sijaan - toimii paremmin eri taajuusalueilla
    int nearestIndex = -1;
    double minCentsDiff = 1e9;

    for (int i = 0; i < 6; ++i)
    {
        double centsDiff = std::abs(1200.0 * std::log2(m_smoothedFrequency / GUITAR_STRINGS[i].frequency));
        if (centsDiff < minCentsDiff)
        {
            minCentsDiff = centsDiff;
            nearestIndex = i;
        }
    }

    if (nearestIndex < 0)
        return nullptr;

    // Jos meillä on lukittu kieli, tarkista pitääkö se vai vaihdetaanko
    if (m_lockedStringIndex >= 0)
    {
        // Laske sentit lukitusta kielestä
        double centsFromLocked = 1200.0 * std::log2(m_smoothedFrequency / GUITAR_STRINGS[m_lockedStringIndex].frequency);

        // Matalat kielet (indeksit 0-2: E low, A, D) tarvitsevat suuremman toleranssin
        // koska niillä on enemmän harmonisia ja taajuusvaihtelu on suurempaa
        double unlockCents = STRING_UNLOCK_CENTS;
        if (m_lockedStringIndex <= 2)
        {
            unlockCents = STRING_UNLOCK_CENTS + 15.0;  // Matalille kielille +15 senttiä
        }

        // Jos ollaan tarpeeksi lähellä lukittua kieltä, pidä se
        if (std::abs(centsFromLocked) < unlockCents)
        {
            // Pysytään lukitussa kielessä
            m_stringLockCount = STRING_LOCK_THRESHOLD; // Pidä lukitus vahvana
            return &GUITAR_STRINGS[m_lockedStringIndex];
        }
        else
        {
            // Taajuus on unlock-alueen ulkopuolella
            // Vähennä lukituslaskuria, mutta vaadi useampi peräkkäinen havainto
            m_stringLockCount--;
            if (m_stringLockCount > 0)
            {
                // Vielä ei vapauteta - pysytään vanhassa
                return &GUITAR_STRINGS[m_lockedStringIndex];
            }
            // Lukitus vapautui, aloitetaan uuden kielen lukitus
            ATLTRACE(_T("String lock released, was: %s (cents off: %.1f)\n"), 
                GUITAR_STRINGS[m_lockedStringIndex].name, centsFromLocked);
            m_lockedStringIndex = -1;
            m_lastDetectedStringIndex = -1;
            m_stringLockCount = 0;
        }
    }

    // Ei lukitusta tai juuri vapautettiin - yritetään lukita uusi kieli
    if (m_lockedStringIndex < 0)
    {
        // Kasvata laskuria jos sama kieli kuin edellisellä kerralla
        if (nearestIndex == m_lastDetectedStringIndex)
        {
            m_stringLockCount++;
            if (m_stringLockCount >= STRING_LOCK_THRESHOLD)
            {
                // Lukitus saavutettu!
                m_lockedStringIndex = nearestIndex;
                ATLTRACE(_T("String locked: %s (%.1f Hz, after %d detections)\n"), 
                    GUITAR_STRINGS[nearestIndex].name, m_smoothedFrequency, m_stringLockCount);
            }
        }
        else
        {
            // Eri kieli, aloita alusta
            m_stringLockCount = 1;
            ATLTRACE(_T("New string detected: %s (was: %d)\n"), 
                GUITAR_STRINGS[nearestIndex].name, m_lastDetectedStringIndex);
        }
        m_lastDetectedStringIndex = nearestIndex;
    }

    // Jos lukitus on aktiivinen, palauta lukittu kieli, muuten lähin
    if (m_lockedStringIndex >= 0)
        return &GUITAR_STRINGS[m_lockedStringIndex];
    else
        return &GUITAR_STRINGS[nearestIndex];
}

// Palauttaa kuinka monta senttiä (cents) ohi lähimmästä/lukitusta kielestä
// Negatiivinen = liian matala, positiivinen = liian korkea
double CPitchDetector::GetCentsOff() const
{
    if (m_smoothedFrequency <= 0 || !m_isValidSignal)
        return 0.0;

    // Käytä lukittua kieltä jos on, muuten etsi lähin
    int stringIndex = m_lockedStringIndex;
    if (stringIndex < 0)
    {
        // Ei lukitusta, etsi lähin
        double minDiff = 1e9;
        for (int i = 0; i < 6; ++i)
        {
            double diff = std::abs(m_smoothedFrequency - GUITAR_STRINGS[i].frequency);
            if (diff < minDiff)
            {
                minDiff = diff;
                stringIndex = i;
            }
        }
    }

    if (stringIndex < 0)
        return 0.0;

    // Cents = 1200 * log2(f1/f2)
    return 1200.0 * std::log2(m_smoothedFrequency / GUITAR_STRINGS[stringIndex].frequency);
}
