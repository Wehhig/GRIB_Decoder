#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <bit>
#include <cmath>

using namespace std;

class WeatherGribReader {
public:
    WeatherGribReader(const string& inputPath, const string& resultPath = "results.txt")
        : sourceFile(inputPath), targetFile(resultPath) {
    }

    void run() {
        vector<unsigned char> dataStream;
        if (!readBinary(sourceFile, dataStream)) return;
        ofstream writer(targetFile);
        if (!writer) return;

        for (size_t idx = 0; idx + 4 < dataStream.size(); ++idx) {
           
            if (dataStream[idx] == '7' && dataStream[idx + 1] == '7'
                && dataStream[idx + 2] == '7' && dataStream[idx + 3] == '7') {
                writer << "Znaleziono 7777 - koniec\n";
                break;
            }
            
            if (dataStream[idx] == 'G' && dataStream[idx + 1] == 'R'
                && dataStream[idx + 2] == 'I' && dataStream[idx + 3] == 'B') {
                processRecord(dataStream, idx, writer);
            }
        }

        writer.close();
    }

private:
    string sourceFile;
    string targetFile;

    bool readBinary(const string& path, vector<unsigned char>& dst) {
        ifstream inStream(path, ios::binary);
        if (!inStream) {
            cerr << "Nie udalo sie otworzyc pliku: " << path << "\n";
            return false;
        }
        dst.assign(istreambuf_iterator<char>(inStream),
            istreambuf_iterator<char>());
        return true;
    }

    unsigned int getUInt(const vector<unsigned char>& buf, size_t pos, size_t length) {
        unsigned int acc = 0;
        for (size_t k = 0; k < length; ++k) {
            acc = (acc << 8) | buf[pos + k];
        }
        return acc;
    }

    int getInt(const vector<unsigned char>& buf, size_t pos, size_t length) {
        int acc = 0;
        bool negative = buf[pos] & 0x80;
        for (size_t k = 0; k < length; ++k) {
            acc = (acc << 8) | buf[pos + k];
        }
        if (negative) {
            int bits = length * 8;
            acc |= -1 << bits;
        }
        return acc;
    }

    double getSignedMagnitude(const vector<unsigned char>& buf, size_t pos) {
        uint32_t rawBits = getUInt(buf, pos, 3);
        bool negFlag = rawBits & 0x800000;
        uint32_t magnitude = rawBits & 0x7FFFFF;
        double degrees = double(magnitude) / 1000.0;
        return negFlag ? -degrees : degrees;
    }

    vector<uint32_t> extractBits(const vector<unsigned char>& buf,size_t startByte,size_t items,size_t bitWidth)
    {
        size_t firstBit = startByte * 8;
        vector<uint32_t> output;
        output.reserve(items);
        for (size_t item = 0; item < items; ++item) {
            uint32_t value = 0;
            for (size_t bit = 0; bit < bitWidth; ++bit) {
                size_t posBit = firstBit + item * bitWidth + bit;
                size_t posByte = posBit / 8;
                size_t offBit = 7 - (posBit % 8);
                uint32_t bitVal = (buf[posByte] >> offBit) & 0x1;
                value = (value << 1) | bitVal;
            }
            output.push_back(value);
        }
        return output;
    }

    void processRecord(const vector<unsigned char>& buf, size_t startIdx, ofstream& writer) {
        size_t cursor = startIdx;
        unsigned int totalLength = getUInt(buf, cursor + 4, 3);
        writer << "Sekcja0 dlugosc=" << totalLength << " bajtow\n";

        unsigned int accLen = 0;
        //PDS
        cursor += 8;
        size_t pdsPos = cursor;
        unsigned int pdsLen = getUInt(buf, pdsPos, 3);
        accLen += pdsLen;
        writer << "Sekcja1 dlugosc=" << pdsLen << "\n";

        unsigned char flags = buf[pdsPos + 7];
        writer << "  GDS=" << boolalpha << bool(flags & 0x80)
            << "  BMS=" << bool(flags & 0x40) << "\n";

        int decScale = getInt(buf, pdsPos + 26, 2);
        writer << "  skala_dz=" << decScale << "\n";

        int yr = buf[pdsPos + 14] + 2000;
        int mon = buf[pdsPos + 13];
        int d = buf[pdsPos + 12];
        int hr = buf[pdsPos + 15];
        int min = buf[pdsPos + 16];
        writer << "  data=" << yr << "-" << mon << "-" << d
            << "  " << hr << ":" << min << "\n";

        cursor += pdsLen;

        //GDS
        if (flags & 0x80) {
            size_t gdsPos = cursor;
            unsigned int gdsLen = getUInt(buf, gdsPos, 3);
            accLen += gdsLen;
            writer << "Sekcja2 dlugosc=" << gdsLen << "\n";

            writer << "  Wiersz/kolumna: "
                << getInt(buf, gdsPos + 6, 2) << ", "
                << getInt(buf, gdsPos + 8, 2) << "\n";

            double lat1 = getSignedMagnitude(buf, gdsPos + 10);
            double lon1 = getSignedMagnitude(buf, gdsPos + 13);
            double lat2 = getSignedMagnitude(buf, gdsPos + 17);
            double lon2 = getSignedMagnitude(buf, gdsPos + 20);
            writer << "  la1=" << lat1 << "  lo1=" << lon1
                << "  la2=" << lat2 << "  lo2=" << lon2 << "\n";

            for (int n = 0; n < 5; ++n) {
                int byteVal = getUInt(buf, gdsPos + 28 + n, 1);
                writer << "  bajt " << (29 + n) << "=" << byteVal << "\n";
            }

            cursor += gdsLen;
        }

        //BDS
        size_t bdsPos = cursor;
        unsigned int bdsLen = getUInt(buf, bdsPos, 3);
        accLen += bdsLen;
        writer << "Sekcja4 dlugosc=" << bdsLen << "\n";

        int binScale = getInt(buf, bdsPos + 4, 2);
        unsigned int refBits = getUInt(buf, bdsPos + 6, 4);
        float refValue;
        memcpy(&refValue, &refBits, sizeof(refValue));
        writer << "  ref_val=" << refValue << "\n";
        writer << "  bits_per=" << int(buf[bdsPos + 10]) << "\n";

        auto words = extractBits(buf, bdsPos + 11, 3, 10);
        for (int j = 0; j < 3; ++j) {
            writer << "  w" << (j + 1) << "=" << words[j] << "\n";
        }
        uint32_t combined = (words[0] << 20) | (words[1] << 10) | words[2];
        writer << "  Polaczone 30 bitow=" << combined << "\n";

        writer << "Suma=" << (8 + accLen) << " dl=" << totalLength << "\n\n";

       
        writer << "Proba odczytu danych:\n";
        for (int k = 0; k < 3; ++k) {
            float value = (refValue + words[k] * pow(2.0, binScale))
                / pow(10.0, decScale);
            writer << "  dana nr " << (k + 1) << "=" << value << "\n";
        }
    }
};

int main() {
    WeatherGribReader reader("20150310.00.W.dwa_griby.grib", "wyniki.txt");
    reader.run();
    return 0;
}
