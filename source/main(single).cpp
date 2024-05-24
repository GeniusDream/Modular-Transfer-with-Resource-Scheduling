 #include <iostream>
#include <fstream>
#include "thpool.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <vector>
#include <lz4.h>
#include <zlib.h>
#include "gzcompress.h"
#include <stdlib.h>
#include <thread>
#include "httplib.h"
#include "cpuTest.h"
#include <bzlib.h>

#define MAX_PATH_LEN (256)
#define FOLDER_IN "/home/c508/IotDataSet/video/150tar"
#define FOLDER_OUT "/home/c508/test/"
int THREAD_NUM1=1;
int THREAD_NUM2=1;
int THREAD_NUM3=1;
#define FILE_MAX_SIZE 167772160 //1024*1024*160
#define FILE_MAX_NUM 5000
//1024      4096     16384   65536   262144  1048576     4194304     16777216
//163840   40960     10240   2560     640      160         40           10
//193840   52960     13240   3160     820      190         52           13
#define BLOCK1_MAX_SIZE 4194304
#define BLOCK1_MAX_NUM 52
#define BLOCK2_MAX_SIZE 4194304+500 //BLOCK2_MAX_SIZE > block2Size
#define BLOCK2_MAX_NUM 52
#define BLOCK3_MAX_SIZE 4194304
#define BLOCK3_MAX_NUM 52

#define ServerIP "192.168.123.128" //"127.0.0.1"

const bool switch_begin = false;

bool clientPort_flag[4];
httplib::Client *clientSet[4];
pthread_mutex_t port_mutex[4];

long long block1Size = BLOCK1_MAX_SIZE;
long long block2Size = BLOCK2_MAX_SIZE-500;
long long block3Size = BLOCK3_MAX_SIZE;
char *testDataBuffer;
char *testDataBuffer2;
long long compressType = 0;
//0 lz4  1 zlib  2 bzip2  3 none  4 gz

struct fileMsg {
    char path[MAX_PATH_LEN], name[MAX_PATH_LEN];
    long long size;

    fileMsg() {}

    fileMsg(const fileMsg &oth) : size(oth.size) {
        memcpy(path, oth.path, MAX_PATH_LEN);
        memcpy(name, oth.name, MAX_PATH_LEN);
    }

    fileMsg(const std::string &fP, const std::string &fN, const long long &fS) : size(fS) {
        strcpy(path, fP.c_str());
        strcpy(name, fN.c_str());
    }

    void set(const std::string &fP, const std::string &fN, const long long &fS) {
        size = fS;
        strcpy(path, fP.c_str());
        strcpy(name, fN.c_str());
    }

    bool operator==(const fileMsg &oth) const {
        return std::string(path) + std::string(name) == std::string(oth.path) + std::string(oth.name);
    }

    bool operator!=(const fileMsg &oth) const {
        return std::string(path) + std::string(name) != std::string(oth.path) + std::string(oth.name);
    }
};

fileMsg *fileStorage_ptr;

struct fileData {
    fileMsg *data;
    fileMsg *final;
    long long size;
    const long long capacity = FILE_MAX_NUM;

    fileData() : data(NULL), final(NULL), size(0) {}

    fileData(fileMsg *ptr) : data(ptr), final(ptr), size(0) {}

    void set(fileMsg *ptr) {
        data = ptr;
        final = ptr;
        size = 0;
    }

    fileMsg *push(const std::string &fP, const std::string &fN, const long long &fS) { //存入文件信息，返回此文件信息指针
        if (final == NULL) {
            std::cout << "fileStorage has no memory allocated" << std::endl;
            return NULL;
        }
        if (size == capacity) {
            std::cout << "fileStorage full" << std::endl;
            return NULL;
        }
        final->set(fP, fN, fS);
        final++;
        size++;
        return final - 1;
    }

    fileMsg &operator[](const long long &idx) { return data[idx]; }
} fileStorage;

struct block1 {
    fileMsg blockFileMsg;
    long long blockNum, blockSize; //this blockNum is equal to position in its file
    char buffer[BLOCK1_MAX_SIZE];

    block1() {}

    block1(const fileMsg &oth, const long long &num, const long long &sz) : blockFileMsg(oth), blockNum(num),
                                                                            blockSize(sz) {}

    void set(const fileMsg &oth, const long long &num, const long long &sz) {
        blockFileMsg = oth;
        blockNum = num;
        blockSize = sz;
    }

    void set(fileMsg *oth, const long long &num, const long long &sz) {
        blockFileMsg.set(oth->path, oth->name, oth->size);
        blockNum = num;
        blockSize = sz;
    }
};

block1 *testBlock1Buffer;
block1 *block1Storage_ptr;

struct block1Data {
    block1 *data;
    block1 *final;
    long long size;
    const long long capacity = BLOCK1_MAX_NUM;

    block1Data() : data(NULL), final(NULL), size(0) {}

    block1Data(block1 *ptr) : data(ptr), final(ptr), size(0) {}

    void set(block1 *ptr) {
        data = ptr;
        final = ptr;
        size = 0;
    }

    char *push(const fileMsg &oth, const long long &num, const long long &sz) {
        if (size == capacity) {
            std::cout << "block1Storage full" << std::endl;
            return NULL;
        }
        final->set(oth, num, sz);
        final++;
        size++;
        return (final - 1)->buffer;
    }

    char *push(fileMsg *oth, const long long &num, const long long &sz) { //存入块信息，并分配数据空间，返回数据空间头指针
        if (size == capacity) {
            std::cout << "block1Storage full" << std::endl;
            return NULL;
        }
        final->set(oth, num, sz);
        final++;
        size++;
        return (final - 1)->buffer;
    }

    block1 &operator[](const long long &idx) { return data[idx]; }
} block1Storage;

struct readFileArg {
    std::string filePath;
    long long posFile, readSize;
    char *posBuffer;

    readFileArg(const std::string &fP, const long long &pF, char *pB, const long long &rS) : filePath(fP), posFile(pF),
                                                                                             posBuffer(pB),
                                                                                             readSize(rS) {}
};

void readFile(readFileArg *arg) {
    std::ifstream filein(arg->filePath, std::ios::in | std::ios::binary);
    filein.seekg(arg->posFile, std::ios::beg);
    filein.read(arg->posBuffer, arg->readSize);
    delete arg;
    return;
}

long long getFileSize(const std::string &filePath) {
    std::ifstream in(filePath, std::ios::in | std::ios::binary);
    std::streampos file_begin, file_end;
    file_begin = in.tellg();
    in.seekg(0, std::ios::end);
    file_end = in.tellg();
    return file_end - file_begin;
}

void addFile(std::string Path, std::string fileName, threadpool &thpool) {
    std::string filePath = Path + '/' + fileName;
    long long fileSize = getFileSize(filePath);
    fileMsg *file = fileStorage.push(Path, fileName, fileSize);
    if (file != NULL) {
        for (long long i = 0; i < fileSize; i += block1Size) {
            char *p = block1Storage.push(file, i, std::min(fileSize - i, block1Size));
            readFileArg *arg = new readFileArg(filePath, i, p, std::min(fileSize - i, block1Size));
            thpool_add_work(thpool, reinterpret_cast<void (*)(void *)>(readFile), arg);
        }
//        std::cout<<"add file succeed: "<<filePath<<std::endl;
    } else {
        std::cout << "add file failed: " << filePath << std::endl;
    }
    return;
}

void trave_dir(const char *path, threadpool &thpool) {
    DIR *d = NULL;
    struct dirent *dp = NULL; /* readdir函数的返回值就存放在这个结构体中 */
    struct stat st;
    char p[MAX_PATH_LEN] = {0};

    if (stat(path, &st) < 0 || !S_ISDIR(st.st_mode)) {
        std::cout << "invalid path: " << (path) << std::endl;
        //printf("invalid path: %s\n", path);
        return;
    }

    if (!(d = opendir(path))) {
        std::cout << "opendir[" << (path) << "] error: " << std::endl;
        //printf("opendir[%s] error: %m\n", path);
        return;
    }

    while ((dp = readdir(d)) != NULL) {
        /* 把当前目录.，上一级目录..及隐藏文件都去掉，避免死循环遍历目录 */
        if ((!strncmp(dp->d_name, ".", 1)) || (!strncmp(dp->d_name, "..", 2)))
            continue;

        //std::cout<<path<<'/'<<dp->d_name<<std::endl;
        snprintf(p, sizeof(p) - 1, "%s/%s", path, dp->d_name);
        stat(p, &st);
        if (!S_ISDIR(st.st_mode)) {
            //std::cout<<dp->d_name<<std::endl;
            addFile(path, (dp->d_name), thpool);
            //printf("%s\n", dp->d_name);
        } else {
            //std::cout<<dp->d_name<<'/'<<std::endl;
            //printf("%s/\n", dp->d_name);
            trave_dir(p, thpool);
        }
    }
    closedir(d);
    return;
}

struct frameHeader {
    long long dataSize;

    frameHeader() {}

    frameHeader(const long long &sz) : dataSize(sz) {}
};

struct block2 {
    long long blockNum, blockSize;
    char buffer[BLOCK2_MAX_SIZE];

    block2() {}

    block2(const long long &num, const long long &sz) : blockNum(num), blockSize(sz) {}

    void set(const long long &num, const long long &sz) {
        blockNum = num;
        blockSize = sz;
    }

    void set(const long long &num) { blockNum = num; }

    frameHeader get_header() { return frameHeader(sizeof(block2)-BLOCK2_MAX_SIZE+blockSize); }
};

block2 *testBlock2Buffer;
block2 *block2Storage_ptr;

struct block2Data {
    block2 *data;
    block2 *final;
    long long size;
    const long long capacity = BLOCK2_MAX_NUM;

    block2Data() : data(NULL), final(NULL), size(0) {}

    block2Data(block2 *ptr) : data(ptr), final(ptr), size(0) {}

    void set(block2 *ptr) {
        data = ptr;
        final = ptr;
        size = 0;
    }

    char *push() { //存入块信息，并分配数据空间，返回数据空间头指针
        if (size == capacity) {
            std::cout << "block2Storage full" << std::endl;
            return NULL;
        }
        final->set(size);
        final++;
        size++;
        return (final - 1)->buffer;
    }

    block2 &operator[](const long long &idx) { return data[idx]; }
} block2Storage;

char *compressQueue1_ptr, *transferQueue2_ptr;
char *compressedBuffer_ptr;

struct compressQueue {
    char *buffer;
    char *final;
    long long size;
    const long long capacity = BLOCK1_MAX_NUM * sizeof(block1);

    compressQueue() : buffer(NULL), final(NULL), size(0) {}

    compressQueue(char *ptr) : buffer(ptr), final(ptr), size(0) {}

    void set(char *ptr) {
        buffer = ptr;
        final = ptr;
        size = 0;
    }

    char *push(char *p, const long long &dataSize) { //将块与块头数据转为char类型，返回此char区段头指针
        if (size + dataSize >= capacity) {
            std::cout << "compressQueue full" << std::endl;
            return NULL;
        }
        memcpy(final, p, dataSize);
        size += dataSize;
        final += dataSize;
        return final - dataSize;
    }
} compressQueue1, transferQueue2;

struct compressArg {
    long long compressSize, blockNum;
    char *src, *dst;
    long long type;

    compressArg(char *s, char *d, const long long &cS, const long long &bN, const long long &ty) : src(s), dst(d),
                                                                                                compressSize(cS),
                                                                                                blockNum(bN),
                                                                                                type(ty) {}
};

void compress_func(compressArg *arg) {
    if (arg->type == 0) { // lz4 compress
        long long compressed_data_size = LZ4_compress_default(arg->src, arg->dst, arg->compressSize,
                                                              LZ4_compressBound(arg->compressSize));
        if (compressed_data_size > BLOCK2_MAX_SIZE) {
            std::cout << "compressed_data_size too big !" << std::endl;
            exit(1);
        }
        block2Storage[arg->blockNum].blockSize = compressed_data_size;
    } else if (arg->type == 1) { // zlib compress
        block2Storage[arg->blockNum].blockSize = compressBound(arg->compressSize);

        if (compress(reinterpret_cast<Bytef *>(arg->dst),
                     reinterpret_cast<uLongf *>(&(block2Storage[arg->blockNum].blockSize)),
                     reinterpret_cast<const Bytef *>(arg->src),
                     arg->compressSize) != 0) {
            std::cout << "compressed fault !" << std::endl;
            exit(1);
        }

        if (block2Storage[arg->blockNum].blockSize > BLOCK2_MAX_SIZE) {
            std::cout << "compressed_data_size too big !" << std::endl;
            exit(1);
        }
    } else if (arg->type == 2) { // bzip2 compress
        block2Storage[arg->blockNum].blockSize = (arg->compressSize) * 1.01 + 600;;
        if (BZ2_bzBuffToBuffCompress((arg->dst),
                                     (unsigned int *)(&block2Storage[arg->blockNum].blockSize),
                                     (arg->src),
                                     (arg->compressSize),
                                     9, 0, 30) != 0) {
            std::cout << "compressed fault !" << std::endl;
            exit(1);
        }
        if (block2Storage[arg->blockNum].blockSize > BLOCK2_MAX_SIZE) {
            std::cout << "compressed_data_size too big !" << std::endl;
            exit(1);
        }
    } else if (arg->type == 3) { // none compress
        block2Storage[arg->blockNum].blockSize = arg->compressSize;
        memcpy(arg->dst, arg->src, arg->compressSize);
    } else if (arg->type == 4) { // gzip compress
        block2Storage[arg->blockNum].blockSize = compressBound(arg->compressSize);
        if (gzcompress((arg->src),
                       (arg->compressSize),
                       (arg->dst),
                       reinterpret_cast<size_t *>(&block2Storage[arg->blockNum].blockSize)) != 0) {
            std::cout << "compressed fault !" << std::endl;
            exit(1);
        }
        if (block2Storage[arg->blockNum].blockSize > BLOCK2_MAX_SIZE) {
            std::cout << "compressed_data_size too big !" << std::endl;
            exit(1);
        }
    }
    memcpy(block2Storage[arg->blockNum].buffer, arg->dst, block2Storage[arg->blockNum].blockSize);
    delete arg;
}

struct block3 {
    long long blockNum, blockSize;
    char buffer[BLOCK3_MAX_SIZE];

    block3() {}

    block3(const long long &num, const long long &sz) : blockNum(num), blockSize(sz) {}

    void set(const long long &num, const long long &sz) {
        blockNum = num;
        blockSize = sz;
    }
};

block3 *block3Storage_ptr;

struct block3Data {
    block3 *data;
    block3 *final;
    long long size;
    const long long capacity = BLOCK3_MAX_NUM;

    block3Data() : data(NULL), final(NULL), size(0) {}

    block3Data(block3 *ptr) : data(ptr), final(ptr), size(0) {}

    void set(block3 *ptr) {
        data = ptr;
        final = ptr;
        size = 0;
    }

    char *push(const long long &sz) { //存入块信息，并分配数据空间，返回数据空间头指针
        if (size == capacity) {
            std::cout << "block3Storage full" << std::endl;
            return NULL;
        }
        final->set(size, sz);
        final++;
        size++;
        return (final - 1)->buffer;
    }

    block3 &operator[](const long long &idx) { return data[idx]; }
} block3Storage;

char *transferQueue1_ptr;

struct transferQueue {
    char *buffer;
    char *final;
    long long size;
    const long long capacity = BLOCK2_MAX_NUM * sizeof(block2);

    transferQueue() : buffer(NULL), final(NULL), size(0) {}

    transferQueue(char *ptr) : buffer(ptr), final(ptr), size(0) {}

    void set(char *ptr) {
        buffer = ptr;
        final = ptr;
        size = 0;
    }

    char *push(char *p, const long long &dataSize) { //将块与块头数据转为char类型，返回此char区段头指针
        if (size + dataSize >= capacity) {
            std::cout << "transferQueue full" << std::endl;
            return NULL;
        }
        memcpy(final, p, dataSize);
        size += dataSize;
        final += dataSize;
        return final - dataSize;
    }
} transferQueue1;

struct transferArg {
    long long blockNum;

    transferArg(const long long &bN) : blockNum(bN) {}
};

void transfer(transferArg *arg) {
    if ((arg->blockNum) != -1) {
        long long clientPort;
        for (clientPort = 0; clientPort < THREAD_NUM3; clientPort++) {
            pthread_mutex_lock(&port_mutex[clientPort]);
            if (!clientPort_flag[clientPort]) {
                clientPort_flag[clientPort] = true;
                pthread_mutex_unlock(&port_mutex[clientPort]);
                break;
            }
            pthread_mutex_unlock(&port_mutex[clientPort]);
        }
        if(auto res = clientSet[clientPort]->Post("/" + std::to_string(arg->blockNum),
                                       block3Storage[arg->blockNum].buffer,
                                                   block3Storage[arg->blockNum].blockSize,
                                                   "text/plain"))
        {
            if(res->status != 200){
                auto err = res.error();
                std::cout << "HTTP error: " << httplib::to_string(err) << std::endl;
            }
        }


        pthread_mutex_lock(&port_mutex[clientPort]);
        clientPort_flag[clientPort] = false;
        pthread_mutex_unlock(&port_mutex[clientPort]);
    }
    delete arg;
}

void solve1(threadpool &thpool, std::string path) {
    trave_dir(path.c_str(), thpool);
    thpool_wait(thpool);
}

void solve2(threadpool &thpool, block1Data *storage) {
    for (long long i = 0; i < (storage->size); i++) {
        compressQueue1.push((char *)((storage->data) + i), sizeof(block1));
    }
    for (long long i = 0, blockNum; i < compressQueue1.size; i += block2Size) {
        char *dst_ptr = compressedBuffer_ptr + block2Storage.size * BLOCK2_MAX_SIZE;
        blockNum = block2Storage.size;
        block2Storage.push();
        compressArg *arg = new compressArg(compressQueue1.buffer + i, dst_ptr,
                                           std::min(compressQueue1.size - i, block2Size), blockNum, compressType);
        thpool_add_work(thpool, reinterpret_cast<void (*)(void *)>(compress_func), arg);
    }
    thpool_wait(thpool);
}

void solve3(threadpool &thpool, block2Data *storage) {
    for (long long i = 0; i < (storage->size); i++) {
        auto header = ((storage->data) + i)->get_header();
        transferQueue1.push((char *)(&header), sizeof(frameHeader));
        transferQueue1.push((char *)((storage->data) + i), header.dataSize);
    }

    for (long long i = 0; i < transferQueue1.size; i += block3Size) {
        char *p = block3Storage.push(std::min(transferQueue1.size - i, block3Size));
        memcpy(p, transferQueue1.buffer + i, std::min(transferQueue1.size - i, block3Size));
        transferArg *arg = new transferArg(block3Storage.size - 1);
        thpool_add_work(thpool, reinterpret_cast<void (*)(void *)>(transfer), arg);
    }
}

void solve3(threadpool &thpool, block1Data *storage) {
    for (long long i = 0; i < (storage->size); i++) {
        transferQueue2.push((char *)((storage->data) + i), sizeof(block1));
    }

    for (long long i = 0; i < transferQueue2.size; i += block3Size) {
        char *p = block3Storage.push(std::min(transferQueue2.size - i, block3Size));
        memcpy(p, transferQueue2.buffer + i, std::min(transferQueue2.size - i, block3Size));
        transferArg *arg = new transferArg(block3Storage.size - 1);
        thpool_add_work(thpool, reinterpret_cast<void (*)(void *)>(transfer), arg);
    }
}

void test1() {
    long long sz = block1Storage.size;
    std::ofstream out;
    std::cout << "block1Num: " << sz << std::endl;
    std::cout << "fileNum: " << fileStorage.size << std::endl;
    for (long long i = 0; i < sz; i++) {
        if (i == 0 || block1Storage[i].blockFileMsg != block1Storage[i - 1].blockFileMsg) {
            if (i)out.close();
            out.open(FOLDER_OUT + std::string(block1Storage[i].blockFileMsg.name),
                     std::ios::out | std::ios::app | std::ios::binary);
        }
        out.write(block1Storage[i].buffer, block1Storage[i].blockSize);
    }
    out.close();
}

void test2() {
    testBlock1Buffer = (block1 *) malloc(1ll * BLOCK1_MAX_NUM * sizeof(block1));
    testDataBuffer = (char *) malloc(FILE_MAX_SIZE);

    char *p = testDataBuffer;
    long long sz = block2Storage.size;
    for (long long i = 0, decompressed_size, rv; i < sz; i++) {
        if (compressType == 0) {
            decompressed_size = LZ4_decompress_safe(block2Storage[i].buffer, p, block2Storage[i].blockSize,
                                                    block2Size);
            if (decompressed_size > 0) {
                block2Storage[i].blockNum = i;
                block2Storage[i].blockSize = decompressed_size;
                p += decompressed_size;
            } else {
                std::cout << "decompressed error : " << decompressed_size << std::endl;
                exit(1);
            }
        } else if (compressType == 1) {
            decompressed_size = block2Size;
            rv = uncompress(reinterpret_cast<Bytef *>(p),
                            reinterpret_cast<uLongf *>(&decompressed_size),
                            reinterpret_cast<const Bytef *>(block2Storage[i].buffer),
                            block2Storage[i].blockSize
            );
            if (rv == 0) {
                block2Storage[i].blockNum = i;
                block2Storage[i].blockSize = decompressed_size;
                p += decompressed_size;
            } else {
                std::cout << "decompressed error : " << rv << std::endl;
                exit(1);
            }
        } else if (compressType == 2) {
            decompressed_size = block2Size;
            rv = gzdecompress(block2Storage[i].buffer, block2Storage[i].blockSize, p,
                              reinterpret_cast<size_t *>(&decompressed_size));
            if (rv == 0) {
                block2Storage[i].blockNum = i;
                block2Storage[i].blockSize = decompressed_size;
                p += decompressed_size;
            } else {
                std::cout << "decompressed error : " << rv << std::endl;
                exit(1);
            }
        } else if (compressType == 3) {
            decompressed_size = block2Storage[i].blockSize;
            memcpy(p, block2Storage[i].buffer, block2Storage[i].blockSize);
            block2Storage[i].blockNum = i;
            block2Storage[i].blockSize = decompressed_size;
            p += decompressed_size;
        }
    }
    sz = p - testDataBuffer;
    memcpy(testBlock1Buffer, testDataBuffer, sz);
    if (sz % sizeof(block1)) {
        std::cout << "depressed size does not match the block size ! " << std::endl;
        exit(1);
    }

    sz = sz / sizeof(block1);
    std::ofstream out;

    for (long long i = 0; i < sz; i++) {
        if (i == 0 || testBlock1Buffer[i].blockFileMsg != testBlock1Buffer[i - 1].blockFileMsg) {
            if (i)out.close();
            out.open(FOLDER_OUT + std::string(testBlock1Buffer[i].blockFileMsg.name), std::ios::out | std::ios::binary);
        }
        out.write(testBlock1Buffer[i].buffer, testBlock1Buffer[i].blockSize);
    }
    out.close();

    free(testDataBuffer);
    free(testBlock1Buffer);
}

void test3(bool compressFlag) {
    testBlock1Buffer = (block1 *) malloc(1ll * BLOCK1_MAX_NUM * sizeof(block1));
    testBlock2Buffer = (block2 *) malloc(1ll * BLOCK2_MAX_NUM * sizeof(block2));
    testDataBuffer = (char *) malloc(FILE_MAX_SIZE);
    testDataBuffer2 = (char *) malloc(FILE_MAX_SIZE);

    long long port = 8080;
    auto client = httplib::Client(ServerIP, port);
    long long sz = block3Storage.size;
    char *p = testDataBuffer2;
    for (long long i = 0; i < sz; i++) {
        if(auto res = client.Get("/"+std::to_string(i))){
            if(res->status == 200){
                memcpy(p, (res->body).c_str(), (res->body).size());
                p += (res->body).size();
            }else{
                auto err = res.error();
                std::cout << "HTTP error: " << httplib::to_string(err) << std::endl;
            }
        }
    }
    sz = p - testDataBuffer2;
    std::cout << "queue size: " << transferQueue1.size << std::endl;
    std::cout << "get size: " << sz << std::endl;
    if (compressFlag) {
        long long cnt=0;
        frameHeader *header;
        p=testDataBuffer2;
        while(p - testDataBuffer2 < sz) {
            header = (frameHeader *)p;
            p += sizeof(frameHeader);
            memcpy(testBlock2Buffer + cnt, p, header->dataSize);
            p += (header->dataSize);
            cnt++;
        }
        if (p - testDataBuffer2 != sz) {
            std::cout << "transfer size does not match the frame header ! " << std::endl;
            exit(1);
        }
        if (cnt != block2Storage.size) {
            std::cout << "transfer count does not match the block2 storage count ! " << std::endl;
            exit(1);
        }

        p = testDataBuffer;

        for (long long i = 0; i < cnt; i++) {
            if (compressType == 0) {
                long long decompressed_size = LZ4_decompress_safe(testBlock2Buffer[i].buffer, p,
                                                                  testBlock2Buffer[i].blockSize, block2Size);
                if (decompressed_size > 0) {
                    testBlock2Buffer[i].blockNum = i;
                    testBlock2Buffer[i].blockSize = decompressed_size;
                    p += decompressed_size;
                } else {
                    std::cout << "decompressed error : " << decompressed_size << std::endl;
                    exit(1);
                }
            } else if (compressType == 1) {
                long long decompressed_size = block2Size;
                long long rv = uncompress(reinterpret_cast<Bytef *>(p),
                                          reinterpret_cast<uLongf *>(&decompressed_size),
                                          reinterpret_cast<const Bytef *>(testBlock2Buffer[i].buffer),
                                          testBlock2Buffer[i].blockSize
                );
                if (rv == 0) {
                    testBlock2Buffer[i].blockNum = i;
                    testBlock2Buffer[i].blockSize = decompressed_size;
                    p += decompressed_size;
                } else {
                    std::cout << "decompressed error : " << rv << std::endl;
                    exit(1);
                }
            } else if (compressType == 2) {
                unsigned int decompressed_size = block2Size;
                long long rv = BZ2_bzBuffToBuffDecompress(p,(&decompressed_size),
                                                          testBlock2Buffer[i].buffer, testBlock2Buffer[i].blockSize,
                                                          0, 0);
                if (rv == 0) {
                    testBlock2Buffer[i].blockNum = i;
                    testBlock2Buffer[i].blockSize = decompressed_size;
                    p += decompressed_size;
                } else {
                    std::cout << "decompressed error : " << rv << std::endl;
                    exit(1);
                }
            } else if (compressType == 3) {
                long long decompressed_size = block2Storage[i].blockSize;
                memcpy(p, block2Storage[i].buffer, block2Storage[i].blockSize);
                block2Storage[i].blockNum = i;
                block2Storage[i].blockSize = decompressed_size;
                p += decompressed_size;
            } else if (compressType == 4) {
                long long decompressed_size = block2Size;
                long long rv = gzdecompress(testBlock2Buffer[i].buffer, testBlock2Buffer[i].blockSize, p,
                                            reinterpret_cast<size_t *>(&decompressed_size));
                if (rv == 0) {
                    testBlock2Buffer[i].blockNum = i;
                    testBlock2Buffer[i].blockSize = decompressed_size;
                    p += decompressed_size;
                } else {
                    std::cout << "decompressed error : " << rv << std::endl;
                    exit(1);
                }
            }
        }
        sz = p - testDataBuffer;
        memcpy(testBlock1Buffer, testDataBuffer, sz);
    } else {
        memcpy(testBlock1Buffer, testDataBuffer2, sz);
    }

    if (sz % sizeof(block1)) {
        std::cout << "depressed size does not match the block size ! " << std::endl;
        exit(1);
    }

    sz = sz / sizeof(block1);
    std::cout << "block1 num: " << sz << std::endl;
    std::ofstream out;
    for (long long i = 0; i < sz; i++) {
        if (i == 0 || testBlock1Buffer[i].blockFileMsg != testBlock1Buffer[i - 1].blockFileMsg) {
            if (i)out.close();
            out.open(FOLDER_OUT + std::string(testBlock1Buffer[i].blockFileMsg.name),
                     std::ios::out | std::ios::app | std::ios::binary);
        }
        out.write(testBlock1Buffer[i].buffer, testBlock1Buffer[i].blockSize);
    }
    out.close();

    free(testBlock1Buffer);
    free(testBlock2Buffer);
    free(testDataBuffer);
    free(testDataBuffer2);
}

void allocate_memory(int op) {
    if(op==1){
        fileStorage_ptr = (fileMsg *) malloc(1ll * FILE_MAX_NUM * sizeof(fileMsg));
        fileStorage.set(fileStorage_ptr);
        block1Storage_ptr = (block1 *) malloc(1ll * BLOCK1_MAX_NUM * sizeof(block1));
        block1Storage.set(block1Storage_ptr);
        compressQueue1_ptr = (char *) malloc((1ll * BLOCK1_MAX_NUM * sizeof(block1)) * sizeof(char));
        compressQueue1.set(compressQueue1_ptr);
    }else if(op==2){
        compressedBuffer_ptr = (char *) malloc((1ll * BLOCK2_MAX_NUM * BLOCK2_MAX_SIZE) * sizeof(char));
        block2Storage_ptr = (block2 *) malloc(1ll * BLOCK2_MAX_NUM * sizeof(block2));
        block2Storage.set(block2Storage_ptr);
    }else if(op==3){
        transferQueue1_ptr = (char *) malloc((1ll * BLOCK2_MAX_NUM * sizeof(block2)) * sizeof(char));
        transferQueue1.set(transferQueue1_ptr);
//    transferQueue2_ptr = (char *) malloc((1ll * BLOCK1_MAX_NUM * sizeof(block1)) * sizeof(char));
//    transferQueue2.set(transferQueue2_ptr);
        block3Storage_ptr = (block3 *) malloc(1ll * BLOCK3_MAX_NUM * sizeof(block3));
        block3Storage.set(block3Storage_ptr);
    }
}

void free_memory(int op) {
    if(op==1){
        free(fileStorage_ptr);
    }else if(op==2){
        free(block1Storage_ptr);
        free(compressQueue1_ptr);
        free(compressedBuffer_ptr);
    }else if(op==3){
        free(block2Storage_ptr);
        free(transferQueue1_ptr);
//        free(transferQueue2_ptr);
        free(block3Storage_ptr);
    }
}

std::time_t getTimeStamp()
{
    std::chrono::time_point<std::chrono::system_clock,std::chrono::milliseconds> tp = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    auto tmp=std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch());
    std::time_t timestamp = tmp.count();
    //std::time_t timestamp = std::chrono::system_clock::to_time_t(tp);
    return timestamp;
}
std::tm* gettm(std::time_t timestamp)
{
    std::time_t milli = timestamp + (std::time_t)8*60*60*1000;//此处转化为东八区北京时间，如果是其它时区需要按需求修改
    auto mTime = std::chrono::milliseconds(milli);
    auto tp=std::chrono::time_point<std::chrono::system_clock,std::chrono::milliseconds>(mTime);
    auto tt = std::chrono::system_clock::to_time_t(tp);
    std::tm* now = std::gmtime(&tt);
    printf("%4d - %02d - %02d - %02d : %02d : %02d.%d",now->tm_year+1900,now->tm_mon+1,now->tm_mday,now->tm_hour,now->tm_min,now->tm_sec, milli%1000);
    return now;
}

void printf_time()
{
    gettm(getTimeStamp());
}

int main(int argc,char *argv[]) {
    compressType=int(argv[1][0]-'0');
    THREAD_NUM1=THREAD_NUM2=THREAD_NUM3=int(argv[2][0]-'0');
//=========================SOLVE1============================
    allocate_memory(1);
    threadpool thpoolInput = thpool_init(THREAD_NUM1);

    std::cout<<"============================begin1============================"<<std::endl;
    if(switch_begin) {
        std::cout<<"Enter any key to begin: "<<std::endl;
        getchar();
        std::cout<<"begin"<<std::endl;
    }
    std::thread cpu1(cpu_begin);
    pthread_t id1 = cpu1.native_handle();
    cpu1.detach();
    auto start_time = std::chrono::high_resolution_clock::now();
    std::cout << "Start time: "; printf_time(); std::cout<<std::endl;

    solve1(thpoolInput, FOLDER_IN);
    thpool_wait(thpoolInput);

    std::cout << "End time: "; printf_time(); std::cout<<std::endl;
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_time = end_time - start_time;
    std::cout << "Running time: " << std::fixed << std::setprecision(1) << elapsed_time.count() * 1000 << " ms" << std::endl;
    std::cout << "Average CPU Usage: " << std::fixed << std::setprecision(1) << cpu_end(id1, elapsed_time.count() * 1000) << std::endl;
    std::cout<<"=============================end1============================="<<std::endl;

    thpool_destroy(thpoolInput);
//    test1();
    free_memory(1);

//=========================SOLVE2============================
    allocate_memory(2);
    threadpool thpoolCompress = thpool_init(THREAD_NUM2);

    std::cout<<"============================begin2============================"<<std::endl;
    if(switch_begin) {
        std::cout<<"Enter any key to begin: "<<std::endl;
        getchar();
        std::cout<<"begin"<<std::endl;
    }
    std::thread cpu2(cpu_begin);
    pthread_t id2 = cpu2.native_handle();
    cpu2.detach();
    start_time = std::chrono::high_resolution_clock::now();
    std::cout << "Start time: "; printf_time(); std::cout<<std::endl;

    solve2(thpoolCompress, &block1Storage);
    thpool_wait(thpoolCompress);

    std::cout << "End time: "; printf_time(); std::cout<<std::endl;
    end_time = std::chrono::high_resolution_clock::now();
    elapsed_time = end_time - start_time;
    std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> tp = std::chrono::time_point_cast<std::chrono::milliseconds>(start_time);
    auto tmp = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch());
    std::cout << "Running time: " << std::fixed << std::setprecision(1) << elapsed_time.count() * 1000 << " ms" << std::endl;
    std::cout << "Average CPU Usage: " << std::fixed << std::setprecision(1) << cpu_end(id2, elapsed_time.count() * 1000) << std::endl;
    long long beginSize = compressQueue1.size;//(block1Storage.size) * sizeof(block1);
    long long endSize = 0;
    for (long long i = 0; i < block2Storage.size; i++) {
        endSize += block2Storage[i].blockSize;
    }
    std::cout << "Compression rate: " << 1.0 * beginSize / endSize << std::endl;
    std::cout<<"=============================end2============================="<<std::endl;

    thpool_destroy(thpoolCompress);
//    test2();
    free_memory(2);

//=========================SOLVE3============================
    allocate_memory(3);
    for (long long i = 0; i < THREAD_NUM3; i++) {
        pthread_mutex_init(&port_mutex[i], NULL);
        clientSet[i] = new httplib::Client(ServerIP, 8080 + i);
        clientPort_flag[i] = false;
    }
    threadpool thpoolTransfer = thpool_init(THREAD_NUM3);

    std::cout<<"============================begin3============================"<<std::endl;
    if(switch_begin) {
        std::cout<<"Enter any key to begin: "<<std::endl;
        getchar();
        std::cout<<"begin"<<std::endl;
    }
    std::thread cpu3(cpu_begin);
    pthread_t id3 = cpu3.native_handle();
    cpu3.detach();
    start_time = std::chrono::high_resolution_clock::now();
    std::cout << "Start time: "; printf_time(); std::cout<<std::endl;

    solve3(thpoolTransfer, &block2Storage);
    thpool_wait(thpoolTransfer);

    std::cout << "End time: "; printf_time(); std::cout<<std::endl;
    end_time = std::chrono::high_resolution_clock::now();
    elapsed_time = end_time - start_time;
    std::cout << "Running time: " << std::fixed << std::setprecision(1) << elapsed_time.count() * 1000 << " ms" << std::endl;
    std::cout << "Average CPU Usage: " << std::fixed << std::setprecision(1) << cpu_end(id3, elapsed_time.count() * 1000) << std::endl;
    std::cout<<"=============================end3============================="<<std::endl;

    thpool_destroy(thpoolTransfer);
    for (long long i = 0; i < THREAD_NUM3; i++) {
        pthread_mutex_destroy(&port_mutex[i]);
        delete clientSet[i];
    }
    std::cout << "queue size: " << transferQueue1.size << std::endl;
//    test3(true);
    free_memory(3);

//=========================OVER============================
    std::cout << "over!" << std::endl;

    return 0;
}
