#include <iostream>
#include <fstream>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <future>
#include <thread>
#include <algorithm>
#include <Windows.h>
using namespace std;

#pragma pack(push, 1)

struct BMPFileHeader {
    uint16_t fileType{ 0x4D42 };
    uint32_t fileSize{ 0 };
    uint16_t reserved1{ 0 };
    uint16_t reserved2{ 0 };
    uint32_t offsetData{ 54 };
};

struct BMPInfoHeader {
    uint32_t size{ 40 };
    int32_t width{ 0 };
    int32_t height{ 0 };
    uint16_t planes{ 1 };
    uint16_t bitCount{ 24 };
    uint32_t compression{ 0 };
    uint32_t imageSize{ 0 };
    int32_t xPixelsPerMeter{ 0 };
    int32_t yPixelsPerMeter{ 0 };
    uint32_t colorsUsed{ 0 };
    uint32_t colorsImportant{ 0 };
};

#pragma pack(pop)

class BMPImage {
private:
    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    uint32_t rowStride;
    vector<uint8_t> pixelData;
    double brightness_factor = 128.0; //если освещенность пикселя(по формуле вычисляется) больше 128 цвет определяется в белый, если меньше в черный

    const char BLACK = '#';
    const char WHITE = ' ';

    [[nodiscard]] int getPixelIndex(int x, int y) const {
        return (y * rowStride) + (x * (infoHeader.bitCount / 8));
    }

    void setPixelBlack(int x, int y) {
        if (x < 0 || x >= infoHeader.width || y < 0 || y >= infoHeader.height)
            return;

        int index = getPixelIndex(x, y);
        pixelData[index] = 0;
        pixelData[index + 1] = 0;
        pixelData[index + 2] = 0;
    }

public:
    void openBMP(const string& fileName) {
        ifstream file(fileName, ios::binary);
        if (!file) {
            throw runtime_error("Ошибка открытия файла: " + fileName);
        }

        file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        if (file.gcount() != sizeof(fileHeader)) throw runtime_error("Ошибка чтения заголовка файла.");

        file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));
        if (file.gcount() != sizeof(infoHeader)) throw runtime_error("Ошибка чтения заголовка информации.");

        if (infoHeader.bitCount != 24 && infoHeader.bitCount != 32) {
            throw runtime_error("Неподдерживаемый формат BMP! Ожидалось 24 или 32 бита.");
        }

        file.seekg(fileHeader.offsetData, ios::beg);

        rowStride = (infoHeader.width * (infoHeader.bitCount / 8) + 3) & ~3;
        pixelData.resize(rowStride * infoHeader.height);
        file.read(reinterpret_cast<char*>(pixelData.data()), pixelData.size());
        if (file.gcount() != pixelData.size()) throw runtime_error("Ошибка чтения пикселей.");
    }

    void saveBMP(const string& fileName) {
        ofstream file(fileName, ios::binary);
        if (!file) {
            throw runtime_error("Ошибка создания файла: " + fileName);
        }

        fileHeader.fileSize = sizeof(fileHeader) + sizeof(infoHeader) + pixelData.size();
        infoHeader.imageSize = pixelData.size();

        file.write(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        file.write(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));

        file.write(reinterpret_cast<char*>(pixelData.data()), pixelData.size());
    }

    [[nodiscard]] bool hasMoreThanTwoColors() const {
        for (int y = 0; y < infoHeader.height; ++y) {
            for (int x = 0; x < infoHeader.width; ++x) {
                int index = getPixelIndex(x, y);
                uint8_t blue = pixelData[index];
                uint8_t green = pixelData[index + 1];
                uint8_t red = pixelData[index + 2];
                if (!(red == 255 && green == 255 && blue == 255) && !(red == 0 && green == 0 && blue == 0))
                    return true;
            }
        }
        return false;
    }

    void convertToBlackAndWhite() {
        auto convertRow = [this](int startRow, int endRow, vector<uint8_t>& newPixelData) {
            for (int y = startRow; y < endRow; ++y) {
                for (int x = 0; x < infoHeader.width; ++x) {
                    int index = getPixelIndex(x, y);

                    uint8_t blue = pixelData[index];
                    uint8_t green = pixelData[index + 1];
                    uint8_t red = pixelData[index + 2];

                    double brightness = 0.2126 * red + 0.7152 * green + 0.0722 * blue;

                    if (brightness < brightness_factor) {
                        newPixelData[index] = 0;
                        newPixelData[index + 1] = 0;
                        newPixelData[index + 2] = 0;
                    }
                    else {
                        newPixelData[index] = 255;
                        newPixelData[index + 1] = 255;
                        newPixelData[index + 2] = 255;
                    }
                }
            }
        };

        vector<uint8_t> newPixelData = pixelData;

        unsigned int numThreads = thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 1;
        int rowsPerThread = infoHeader.height / numThreads;
        vector<future<void>> futures;

        for (unsigned int i = 0; i < numThreads; ++i) {
            int startRow = i * rowsPerThread;
            int endRow = (i == numThreads - 1) ? infoHeader.height : startRow + rowsPerThread;
            futures.push_back(async(launch::async, convertRow, startRow, endRow, ref(newPixelData)));
        }

        for (auto& future : futures) {
            future.get();
        }

        pixelData = move(newPixelData);
    }

    void drawLine(int x1, int y1, int x2, int y2) {
        int dx = abs(x2 - x1);
        int dy = abs(y2 - y1);
        int sx = (x1 < x2) ? 1 : -1;
        int sy = (y1 < y2) ? 1 : -1;
        int err = dx - dy;

        while (true) {
            setPixelBlack(x1, y1);

            if (x1 == x2 && y1 == y2) break;

            int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x1 += sx;
            }
            if (e2 < dx) {
                err += dx;
                y1 += sy;
            }
        }
    }

    void drawCross() {
        drawLine(0, 0, infoHeader.width - 1, infoHeader.height - 1);

        drawLine(infoHeader.width - 1, 0, 0, infoHeader.height - 1);
    }

    void displayBMP(bool drawCross = false) {
        if (hasMoreThanTwoColors()) {
            cout << "Изображение содержит более двух цветов, конвертируем в черно-белое..." << endl;
            convertToBlackAndWhite();
        }

        if (drawCross) {
            cout << "Рисуем крест (X) на изображении..." << endl;
            this->drawCross();
        }

        for (int y = infoHeader.height - 1; y >= 0; y -= 2) {
            for (int x = 0; x < infoHeader.width; ++x) {
                int index = getPixelIndex(x, y);
                uint8_t blue = pixelData[index];
                uint8_t green = pixelData[index + 1];
                uint8_t red = pixelData[index + 2];

                cout << ((red == 255 && green == 255 && blue == 255) ? WHITE : BLACK);
            }
            cout << endl;
        }
    }
};

int main() {
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);
    string a, b;
    BMPImage bmp;
    cout << "Enter input BMP file name: " << endl;
    cin >> a;
    try {
        bmp.openBMP(a);

        cout << "Исходное изображение:" << endl;
        bmp.displayBMP(false);

        cout << "Изображение с крестом (X): \n" << endl;
        bmp.drawCross();
        bmp.displayBMP(false);

        cout << "\nEnter output BMP file name: " << endl;
        cin >> b;
        bmp.saveBMP(b);
        cout << "\nИзображение с крестом сохранено в " << b << endl;
    }
    catch (const exception& e) {
        cerr << "Ошибка: " << e.what() << endl;
        return 1;
    }
    return 0;
}