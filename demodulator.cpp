#include "demodulator.h"

Demodulator::Demodulator(int inSempleRate) {
    numOfChannels = SAMPLE_RATE / CHANNEL_SAMPLE_RATE * X2;
    decimationK = inSempleRate / SAMPLE_RATE;

    std::cout << "Demodulator init. Num of channels: " << numOfChannels << ". decimationK = " << decimationK << std::endl;

    channeslData = new std::vector<std::complex<float>>[numOfChannels];
    fftw_in = (fftwf_complex*) fftwf_malloc (numOfChannels * sizeof(fftwf_complex));
    fftw_out = (fftwf_complex*) fftwf_malloc (numOfChannels * sizeof(fftwf_complex));
    fftw_p = fftwf_plan_dft_1d(numOfChannels, fftw_in, fftw_out, FFTW_FORWARD, FFTW_ESTIMATE);

    i1 = new float[numOfChannels];
    q1 = new float[numOfChannels];

    guiTh = new std::thread(&Demodulator::guiThread, this, 0, nullptr);
    speedTh = new std::thread(&Demodulator::freqCounter, this);

    scg.setFreq(0);
    scg.setSampleRate(1000000);

    dec = new Decoder();
}

Demodulator::~Demodulator() {
    fftwf_free(fftw_in);
    fftwf_free(fftw_out);
    fftwf_destroy_plan(fftw_p);
    delete[] i1;
    delete[] q1;
}

float a = 0.0;

void Demodulator::addIQ(void* data, int size) {
    //printf("addIQ %d\n", size);
    float* dataPtr = (float*)data;
    //dataPtr[0] = 10;
    if (window)
        scg.setFreq(window->getSelectedFreq());


    wideSpectorData.insert(wideSpectorData.end(), dataPtr, dataPtr + size*2);
    wideSpectorDataSamples += size;

    for (int i=0; i<wideSpectorData.size(); i += 2) {
        wideSpectorData[i] *= 2 * scg.nextCos();
        wideSpectorData[i + 1] *= 2 * scg.nextSin();
        scg.nextStep();
    }

    decimation();
    channelize();

    char tmp[numOfChannels * sizeof(float) * 2];
    memcpy(tmp, fftw_out + numOfChannels/2, sizeof(tmp) / 2);
    memcpy(tmp + sizeof(tmp) / 2, fftw_out, sizeof(tmp) / 2);

    if (window) {
        window->push1MHzData(fftw_out, numOfChannels);
    }

    std::cout << channeslData[0].size() << std::endl;
    if (channeslData[0].size() > 100) {
        for (int i=0; i<PreamblePointWithoutFullData.size(); i++) {
            if (PreamblePointWithoutFullData[i]->data.size() >= (DATA_LEN + PREAMBLE_LEN + 1)) {
                //PreamblePointFullData.push_back(PreamblePointWithoutFullData[i]);
                dec->pushPreablePoint(PreamblePointWithoutFullData[i]);
                PreamblePointWithoutFullData.erase(PreamblePointWithoutFullData.begin() + i);
            } else {
                PreamblePointWithoutFullData[i]->data.insert(PreamblePointWithoutFullData[i]->data.end(), channeslData[PreamblePointWithoutFullData[i]->channel].begin() + PREAMBLE_LEN, channeslData[PreamblePointWithoutFullData[i]->channel].end());
            }



        }

        for (int i=0; i<numOfChannels; i++) {
            //std::cout << "ch = " << i << std::endl;
            findPreambles(i);
            //channeslData[i].clear();
            channeslData[i].erase(channeslData[i].begin(), channeslData[i].end() - 32);
        }
    }
}

float dds = 0;

void Demodulator::decimation() {
    fftwf_complex* dataIn = (fftwf_complex*)wideSpectorData.data();
    dds += 1;
    int i;

    for (i=0; i < (int)(wideSpectorData.size()/2) - BL_1M_to_125K - decimationK; i += decimationK) {
        float iC = 0.0;
        float qC = 0.0;

        for (int j=0; j<BL_1M_to_125K; j++) {
            iC += dataIn[i + j][0] * B_1M_to_125K[j];
            qC += dataIn[i + j][1] * B_1M_to_125K[j];
        }

        decimatedData.push_back(iC);
        decimatedData.push_back(qC);

        decimatedDataSamples++;
    }

    if (i !=0) {
        if (window) {
            //window->push100HzData(decimatedData.data());
        }
        wideSpectorData.erase(wideSpectorData.begin(), wideSpectorData.begin() + i*2);
    }
}

float ou = 0;

void Demodulator::channelize() {
    fftwf_complex* dataIn = (fftwf_complex*)decimatedData.data();

    int i;

    //std::cout << (int)(decimatidData.size()/2) - BL_125K_to_100 - numOfChannels*BL_125K_to_100/numOfChannels << std::endl;

    for (i=0; i < (int)(decimatedData.size()/2) - BL_125K_to_100 - numOfChannels*BL_125K_to_100/numOfChannels - numOfChannels/2; i += numOfChannels/2) {
        for (unsigned int ch=0; ch < numOfChannels; ch++) {
            float iC = 0.0;
            float qC = 0.0;

            for (int j=0; j < BL_125K_to_100/numOfChannels; j++) {
                iC += dataIn[i + ch + j*numOfChannels][0] * B_125K_to_100[ch + j*numOfChannels];
                qC += dataIn[i + ch + j*numOfChannels][1] * B_125K_to_100[ch + j*numOfChannels];
            }


            if (invers) {
                if (ch < numOfChannels/2) {
                    fftw_in[ch + numOfChannels/2][0] = iC;
                    fftw_in[ch + numOfChannels/2][1] = qC;
                }
                else {
                    fftw_in[ch - numOfChannels/2][0] = iC;
                    fftw_in[ch - numOfChannels/2][1] = qC;
                }
            }
            else {
                fftw_in[ch][0] = iC;
                fftw_in[ch][1] = qC;
            }


            //fftw_in[ch][0] = iC;
            //fftw_in[ch][1] = qC;
        }

        fftwf_execute(fftw_p);

        channeslDataSamples++;

        if (window) {
            window->push100HzData(fftw_out[window->getSelectedChannel()]);
        }

        for (int ch=0; ch < numOfChannels; ch++) {
            std::complex<float> c(fftw_out[ch][0], fftw_out[ch][1]);

            channeslData[ch].push_back(c);
        }

        invers = !invers;
    }

    //printf("channeslData %d %d\n", i / (numOfChannels/2), channeslData[0].size() * numOfChannels);

    if (i != 0) {
        //i -= numOfChannels/2;
        decimatedData.erase(decimatedData.begin(), decimatedData.begin() + i*2);
    }
}

int Demodulator::bitDif(uint32_t a, uint32_t b) {
    int ret = 0;
    uint64_t c = a ^ b;
    for (int i=0; i<64; i++) {
        if ((c >> i) & 1)
            ret++;
    }

    return ret;
}

void Demodulator::findPreambles(int ch) {
    //float i1=0, q1=0;
//    if (pp[ch]) {
//        if (pp[ch]->data.size() + channeslData[ch].size() > 260) {
//            pp[ch]->data.insert(pp[ch]->data.end(), channeslData[ch].begin(), channeslData[ch].begin() + (260 - pp[ch]->data.size()));
//        }
//    }
//    if (ch == 27) {
//        int a;
//        a++;
//    }

    for (int x=32; x < channeslData[ch].size(); x += SYM_LEN) {
        i2[ch] = channeslData[ch][x].imag();
        q2[ch] = channeslData[ch][x].real();

        corr1[ch] = q1[ch]*q2[ch] + i1[ch]*i2[ch];
        corr2[ch] = -i1[ch]*q2[ch] + q1[ch]*i2[ch];

        corr[ch] = (corr[ch] >> 1) | ((corr1[ch] > 0 ? 1:0) << 31);
        inv_corr[ch] = (inv_corr[ch] >> 1) | ((corr2[ch] > 0 ? 1:0) << 31);

        i1[ch] = i2[ch];
        q1[ch] = q2[ch];
        
        //const uint32_t prea = 0xACFF00AA;0x97157A6F
        const uint32_t prea = 0x97157A6F;

        int err1 =  bitDif(corr[ch], prea);
        int err2 =  bitDif(inv_corr[ch], (~prea));
        if (err1 <= MAX_ERRORS) {
            std::cout << "Preamble!!!" << std::endl;
            std::cout << " Channel: " << ch << std::endl;
            std::cout << " Errors: " << err1 << std::endl;
            std::cout << " Data: " << uint32ToSring(corr[ch]) << std::endl;
            std::cout << " Expt: " << uint32ToSring(prea) << std::endl;

            PreamblePoint* pp = new PreamblePoint;
            pp->channel = ch;
            pp->inv = 0;
            pp->data.insert(pp->data.begin(), channeslData[ch].begin() + x - 32,channeslData[ch].begin() + x - 32 + std::min(channeslData[ch].size() - x + 32, (size_t)(DATA_LEN + PREAMBLE_LEN + 1)));
            PreamblePointWithoutFullData.push_back(pp);
        }

        if (err2 <= MAX_ERRORS) {
            std::cout << "Preamble!!2" << std::endl;
            std::cout << " Channel: " << ch << std::endl;
            std::cout << " Errors: " << err2 << std::endl;
            std::cout << " Data: " << uint32ToSring(inv_corr[ch]) << std::endl;
            std::cout << " Expt: " << uint32ToSring(~prea) << std::endl;

            PreamblePoint* pp = new PreamblePoint;
            pp->channel = ch;
            pp->inv = PreamblePoint::invIQ;
            pp->data.insert(pp->data.begin(), channeslData[ch].begin() + x - 32,channeslData[ch].begin() + x - 32 + std::min(channeslData[ch].size() - x + 32, (size_t)(DATA_LEN + PREAMBLE_LEN + 1)));
            PreamblePointWithoutFullData.push_back(pp);
        }

        err1 =  bitDif(~corr[ch], prea);
        err2 =  bitDif(~inv_corr[ch], (~prea));

        if (err1 <= MAX_ERRORS) {
            std::cout << "Preamble inv!!!" << std::endl;
            std::cout << " Channel: " << ch << std::endl;
            std::cout << " Errors: " << err1 << std::endl;
            std::cout << " Data: " << uint32ToSring(corr[ch]) << std::endl;
            std::cout << " Expt: " << uint32ToSring(prea) << std::endl;

            PreamblePoint* pp = new PreamblePoint;
            pp->channel = ch;
            pp->inv = PreamblePoint::invBits;
            pp->data.insert(pp->data.begin(), channeslData[ch].begin() + x - 32,channeslData[ch].begin() + x - 32 + std::min(channeslData[ch].size() - x + 32, (size_t)(DATA_LEN + PREAMBLE_LEN + 1)));
            PreamblePointWithoutFullData.push_back(pp);
        }

        if (err2 <= MAX_ERRORS) {
            std::cout << "Preamble inv!!2" << std::endl;
            std::cout << " Channel: " << ch << std::endl;
            std::cout << " Errors: " << err2 << std::endl;
            std::cout << " Data: " << uint32ToSring(inv_corr[ch]) << std::endl;
            std::cout << " Expt: " << uint32ToSring(~prea) << std::endl;

            PreamblePoint* pp = new PreamblePoint;
            pp->channel = ch;
            pp->inv = PreamblePoint::invIQ | PreamblePoint::invIQ;
            pp->data.insert(pp->data.begin(), channeslData[ch].begin() + x - 32,channeslData[ch].begin() + x - 32 + std::min(channeslData[ch].size() - x + 32, (size_t)(DATA_LEN + PREAMBLE_LEN + 1)));
            PreamblePointWithoutFullData.push_back(pp);
        }
    }
}

void Demodulator::guiThread(int argc, char *argv[]) {
    QApplication a(argc, argv);
    window = new MainWindow();
    window->show();
    a.exec();

    exit(0);
}

uint64_t Demodulator::toDifCoding64(uint64_t a) {
    uint64_t ret = 0;
    for (int i=62; i >= 0; i--) {
        uint8_t tmp = (a >> i) & 0b11;
        ret <<= 1;
        if (tmp == 0b11 || tmp == 0b00 ) {
            ret |= 0;
        }
        else {
            ret |= 1;
        }
        //ret = (ret << 1) | ((a >> i) != (a >> (i - 1)));
    }

    return ret;
}

void Demodulator::freqCounter() {
    const int m = 7;
    int lastWD = 0, lastDD = 0, lastCD = 0;
    int avgWD[m], avgDD[m], avgCD[m];
    int _avgWD, _avgDD, _avgCD;
    int index = 0;
    while (1) {
        avgWD[index] = wideSpectorDataSamples - lastWD;
        avgDD[index] = decimatedDataSamples - lastDD;
        avgCD[index] = channeslDataSamples - lastCD;
        index = (index + 1) % m;

        _avgWD = _avgDD = _avgCD = 0;
        for (int i=0; i<m; i++) {
            _avgWD += avgWD[i];
            _avgDD += avgDD[i];
            _avgCD += avgCD[i];
        }

        std::cout << " WD = " << _avgWD/m << std::endl;
        std::cout << " DD = " << _avgDD/m << std::endl;
        std::cout << " CD = " << _avgCD/m << std::endl;

        lastWD = wideSpectorDataSamples;
        lastDD = decimatedDataSamples;
        lastCD = channeslDataSamples;

        usleep(1000000);
    }
}

std::string Demodulator::uint32ToSring(uint32_t a) {
    std::string ret = "";

    for (int i=0; i<sizeof(uint32_t) * 8; i++) {
        ret.push_back((a >> (sizeof(uint32_t) * 8 - i - 1)) & 1 ? '1' : '0');
    }

    return ret;
}

void Demodulator::dumpToFile(std::string fileName, void *data, size_t size) {
    std::fstream file(fileName);
    if (file.is_open()) {
        file.write((char*)data, size);
        file.close();
    }
}