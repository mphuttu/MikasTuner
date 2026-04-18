#include "pch.h"
#include "CPitchDetector.h"
#include <cmath>

#include <numeric>
#define NOMINMAX
#include <Windows.h>
#undef min 
#undef max
#include <algorithm>


// Kitaran standardiviritys (E2, A2, D3, G3, B3, E4)
const CPitchDetector::GuitarString CPitchDetector::GUITAR_STRINGS[6] = {
    { L"E (matala)", 82.41 },
    { L"A", 110.00 },
    { L"D", 146.83 },
    { L"G", 196.00 },
    { L"B (H)", 246.94 },
    { L"E (korkea)", 329.63 }
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

    double diff = std::abs(newFrequency - m_lastValidFrequency);
    return diff < FREQUENCY_JUMP_THRESHOLD;
}

void CPitchDetector::UpdateSmoothedFrequency(double newFrequency)
{
    // Lisätään historiaan
    m_frequencyHistory.push_back(newFrequency);
    if (m_frequencyHistory.size() > HISTORY_SIZE)
    {
        m_frequencyHistory.pop_front();
    }

    // Tarkista onko taajuus vakaa (ei suuria hyppyjä)
    if (!IsFrequencyStable(newFrequency))
    {
        m_stableCount = 0;
        // Älä päivitä tasoitettua taajuutta vielä, odota vakaata signaalia
        return;
    }

    m_stableCount++;

    // Vaadi muutama peräkkäinen vakaa mittaus ennen päivitystä
    if (m_stableCount < STABLE_COUNT_THRESHOLD)
    {
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

    // Lasketaan autokorrelaatio ja normalisoidaan lag=0:lla
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
        autocorrelation[lag] = sum / autocorrAtZero; // Normalisoitu 0-1 välille
    }

    // 4. Etsitään suurin piikki kitaran taajuusalueelta
    int minLag = m_sampleRate / static_cast<int>(MAX_GUITAR_FREQ); // ~350 Hz
    int maxLagSearch = m_sampleRate / static_cast<int>(MIN_GUITAR_FREQ); // ~75 Hz

    if (maxLagSearch > maxLag)
        maxLagSearch = maxLag;

    if (minLag < 1)
        minLag = 1;

    int peakLag = -1;
    double peakValue = 0.0;

    for (int lag = minLag; lag < maxLagSearch - 1; ++lag)
    {
        // Etsitään paikallinen maksimi
        if (autocorrelation[lag] > autocorrelation[lag - 1] &&
            autocorrelation[lag] > autocorrelation[lag + 1])
        {
            // Vaaditaan merkittävä piikki (vähintään 0.2)
            if (autocorrelation[lag] > peakValue && autocorrelation[lag] > 0.2)
            {
                peakValue = autocorrelation[lag];
                peakLag = lag;
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

    // 6. Parabolinen interpolointi tarkemmalle taajuudelle
    double refinedLag = ParabolicInterpolation(autocorrelation, peakLag);

    // 7. Lasketaan taajuus
    return static_cast<double>(m_sampleRate) / refinedLag;
}

const CPitchDetector::GuitarString* CPitchDetector::GetNearestString() const
{
    if (m_smoothedFrequency <= 0 || !m_isValidSignal)
        return nullptr;

    const GuitarString* nearest = nullptr;
    double minDiff = 1000.0;

    for (int i = 0; i < 6; ++i)
    {
        double diff = std::abs(m_smoothedFrequency - GUITAR_STRINGS[i].frequency);
        if (diff < minDiff)
        {
            minDiff = diff;
            nearest = &GUITAR_STRINGS[i];
        }
    }
    return nearest;
}

// Palauttaa kuinka monta senttiä (cents) ohi lähimmästä kielestä
// Negatiivinen = liian matala, positiivinen = liian korkea
double CPitchDetector::GetCentsOff() const
{
    const GuitarString* nearest = GetNearestString();
    if (!nearest || m_smoothedFrequency <= 0)
        return 0.0;

    // Cents = 1200 * log2(f1/f2)
    return 1200.0 * std::log2(m_smoothedFrequency / nearest->frequency);
}
