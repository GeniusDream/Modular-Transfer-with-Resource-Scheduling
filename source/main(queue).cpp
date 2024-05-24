#include <iostream>
#include <fstream>
#include "thpool.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <lz4.h>
#include <zlib.h>
#include "gzcompress.h"
#include <stdlib.h>
#include <mutex>
#include <condition_variable>
#include <utility>
#include <pthread.h>
#include "thread_safe_queue.h"
#include <thread>
#include <functional>
#include <unistd.h>
#include <iomanip>
#include "httplib.h"
#include "cpuTest.h"
#include <bzlib.h>

#define MAX_PATH_LEN (256)
#define FOLDER_IN "/home/c508/IotDataSet/time-scenario/150"
#define FOLDER_OUT "/home/c508/test/"
#define THREAD_MAX_NUM1 4
#define THREAD_MAX_NUM2 4
#define THREAD_MAX_NUM3 4
#define FILE_MAX_SIZE 167772160
#define FILE_MAX_NUM 5000

//#define monitor_show
//1024      4096     16384   65536   262144  1048576     4194304     16777216
//163840   40960     10240   2560     640      160         40           10
#define BLOCK1_MAX_SIZE 16384 //2^16
#define BLOCK1_MAX_NUM 13240
#define BLOCK2_MAX_SIZE 65536+3000 //BLOCK2_MAX_SIZE > block2Size
#define BLOCK2_MAX_NUM 3160
#define BLOCK3_MAX_SIZE 65536
#define BLOCK3_MAX_NUM 3160
//#define QUEUE1_SIZE 8388608
//#define QUEUE2_SIZE 8388608
#define QUEUE_NUM0 1
#define QUEUE_NUM1 1
#define QUEUE_NUM2 1
//每隔TIMESPAN微妙
#define TIMESPAN 100000
#define ServerIP "192.168.123.128" //"127.0.0.1"

//#define cT 3
//0 lz4  1 zlib  2 bzip2  3 none  4 gz

threadpool thpoolMonitor;
threadpool thpoolQueues0;
threadpool thpoolQueues1;
threadpool thpoolQueues2;
threadpool thpoolInput[THREAD_MAX_NUM1];
threadpool thpoolCompress[THREAD_MAX_NUM2];
threadpool thpoolTransfer[THREAD_MAX_NUM3];

long long block1Size = BLOCK1_MAX_SIZE;
long long block2Size = BLOCK2_MAX_SIZE-3000;
long long block3Size = BLOCK3_MAX_SIZE;
char *testDataBuffer;
char *testDataBuffer2;
//long long cT = 3;
//0 lz4  1 zlib  2 bzip2  3 none  4 gz

bool clientPort_flag[THREAD_MAX_NUM3];
httplib::Client *clientSet[THREAD_MAX_NUM3];
pthread_mutex_t port_mutex[THREAD_MAX_NUM3];

bool read_end = false, compress_end = false, transfer_end = false;
bool add1_end = false, add2_end = false, add3_end = false;
long long block1_cnt = 0, block2_cnt = 0, block3_cnt = 0;
long long finish1_cnt = 0, finish2_cnt = 0, finish3_cnt = 0;
long long control1[2] = {1, 0}, control2[3] = {1, 0, 0}, control3[2] = {1, 0};
pthread_mutex_t block1_mutex, block2_mutex, block3_mutex;
pthread_mutex_t finish1_mutex, finish2_mutex, finish3_mutex;
pthread_mutex_t add1_mutex, add2_mutex, add3_mutex;
pthread_mutex_t compressTSQ_mutex, transferTSQ_mutex;
pthread_mutex_t control1_mutex, control2_mutex[2], control3_mutex;

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

struct queuePopMsg {
    char *ptr;
    long long popSize;

    queuePopMsg() {}

    queuePopMsg(char *p, long long pS) : ptr(p), popSize(pS) {}
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

block1 *memBlock1Buffer;
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

    block1 *push(const fileMsg &oth, const long long &sz) {
        if (size == capacity) {
            std::cout << "block1Storage full" << std::endl;
            return NULL;
        }
        final->set(oth, size, sz);
        final++;
        size++;
        return (final - 1);
    }

    block1 *push(fileMsg *oth, const long long &sz) { //存入块信息，并分配数据空间，返回数据空间头指针
        if (size == capacity) {
            std::cout << "block1Storage full" << std::endl;
            return NULL;
        }
        final->set(oth, size, sz);
        final++;
        size++;
        return (final - 1);
    }

    block1 &operator[](const long long &idx) { return data[idx]; }
} block1Storage;

char *compressQueue1_ptr;

struct compressQueue {
    char *buffer;
    char *final, *front;
    int size, popSize, cntBlock;
    const int capacity = BLOCK1_MAX_NUM * sizeof(block1);

    compressQueue() : buffer(NULL), final(NULL), front(NULL), size(0), popSize(0), cntBlock(0) {}

    compressQueue(char *ptr) : buffer(ptr), final(ptr), front(ptr), size(0), popSize(0), cntBlock(0) {}

    void set(char *ptr) {
        buffer = ptr;
        final = ptr;
        front = ptr;
        size = popSize = 0;
        cntBlock = 0;
    }

    bool empty() { return front == final; }

    void push(char *p, const int &dataSize) { //将块与块头数据转为char类型，返回此char区段头指针
        if (size + dataSize >= capacity) {
            std::cout << "compressQueue full" << std::endl;
            return;
        }
        memcpy(final, p, dataSize);
        cntBlock++;
        size += dataSize;
        final += dataSize;
    }

    queuePopMsg pop(const int &dataSize) {
        long long sz = std::min(dataSize, size - popSize);
        queuePopMsg ans = queuePopMsg(front, sz);
        front += sz;
        popSize += sz;
        return ans;
    }

    bool hasSize(const int &dataSize) {
        return size - popSize >= dataSize;
    }
} compressQueue1;

ThreadSafeQueue<long long> compressTSQ;
long long compressTSQ_cnt = 0;

struct readFileArg {
    std::string filePath;
    long long posFile, readSize;
    block1 *posBlock1;

    readFileArg(const std::string &fP, const long long &pF, block1 *pB, const long long &rS) : filePath(fP),
                                                                                               posFile(pF),
                                                                                               posBlock1(pB),
                                                                                               readSize(rS) {}
};

void readFile(readFileArg *arg) {
    if ((arg->posBlock1) != NULL) {
        std::ifstream filein(arg->filePath, std::ios::in | std::ios::binary);
        filein.seekg(arg->posFile, std::ios::beg);
        filein.read(arg->posBlock1->buffer, arg->readSize);
        pthread_mutex_lock(&compressTSQ_mutex);
        compressTSQ_cnt++;
        pthread_mutex_unlock(&compressTSQ_mutex);
        compressTSQ.push(arg->posBlock1->blockNum);
    }

    pthread_mutex_lock(&finish1_mutex);
    finish1_cnt += (arg->readSize);
    pthread_mutex_unlock(&finish1_mutex);

    pthread_mutex_lock(&block1_mutex);
    block1_cnt--;
    pthread_mutex_lock(&add1_mutex);
    if (add1_end && (block1_cnt == 0)) {
        pthread_mutex_lock(&compressTSQ_mutex);
        read_end = true;
        pthread_mutex_unlock(&compressTSQ_mutex);
        compressTSQ.push(-1);
    }
    pthread_mutex_unlock(&add1_mutex);
    pthread_mutex_unlock(&block1_mutex);

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

void addFile(std::string Path, std::string fileName, threadpool *thpool) {
    std::string filePath = Path + '/' + fileName;
    long long fileSize = getFileSize(filePath);
    fileMsg *file = fileStorage.push(Path, fileName, fileSize);
    threadpool *nowThpool;
    if (file != NULL) {
        for (long long i = 0; i < fileSize; i += block1Size) {
            block1 *p = block1Storage.push(file, std::min(fileSize - i, block1Size));
            readFileArg *arg = new readFileArg(filePath, i, p, std::min(fileSize - i, block1Size));
            while (true) {
                pthread_mutex_lock(&control1_mutex);
                if (control1[0] && control1[1]>0) {
                    nowThpool = thpool + control1[0] - 1;
                    control1[1] -= arg->readSize;
                    pthread_mutex_unlock(&control1_mutex);
                    break;
                }
                pthread_mutex_unlock(&control1_mutex);
            }
            pthread_mutex_lock(&block1_mutex);
            block1_cnt++;
            pthread_mutex_unlock(&block1_mutex);
            thpool_add_work(*nowThpool, reinterpret_cast<void (*)(void *)>(readFile), arg);
        }
//        std::cout<<"add file succeed: "<<filePath<<std::endl;
    } else {
        std::cout << "add file failed: " << filePath << std::endl;
    }
    return;
}

struct trave_dirArg {
    const char *path;
    threadpool *thpool;

    trave_dirArg(const char *_path, threadpool *_thpool) : path(_path), thpool(_thpool) {}
};

void trave_dir(const char *path, threadpool *thpool) {
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

    threadpool *nowThpool;
    while (true) {
        pthread_mutex_lock(&control1_mutex);
        if (control1[0] && control1[1]>0) {
            nowThpool = thpool + control1[0] - 1;
            pthread_mutex_unlock(&control1_mutex);
            break;
        }
        pthread_mutex_unlock(&control1_mutex);
    }
    pthread_mutex_lock(&add1_mutex);
    add1_end = true;
    pthread_mutex_unlock(&add1_mutex);
    readFileArg *readArg = new readFileArg("", -1, NULL, -1);
    pthread_mutex_lock(&block1_mutex);
    block1_cnt++;
    pthread_mutex_unlock(&block1_mutex);
    thpool_add_work(*nowThpool, reinterpret_cast<void (*)(void *)>(readFile), readArg);
    return;
}

struct frameHeader {
    long long dataSize;

    frameHeader() {}

    frameHeader(const long long &sz) : dataSize(sz) {}
};

struct block2 {
    long long blockNum, blockSize, compressType;
    char buffer[BLOCK2_MAX_SIZE];

    block2() {}

    block2(const long long &num, const long long &sz, const long long &ct) : blockNum(num), blockSize(sz), compressType(ct) {}

    void set(const long long &num, const long long &sz, const long long &ct) {
        blockNum = num;
        blockSize = sz;
        compressType = ct;
    }

    void set(const long long &num) { blockNum = num; }

    frameHeader get_header() { return frameHeader(sizeof(block2)-BLOCK2_MAX_SIZE+blockSize); }
};

block2 *memBlock2Buffer;
block2 *testBlock2Buffer;
block2 *block2Storage_ptr;

struct block2Data {
    block2 *data;
    block2 *final;
    int size;
    const int capacity = BLOCK2_MAX_NUM;

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

char *compressedBuffer_ptr;

char *transferQueue1_ptr;

struct transferQueue {
    char *buffer;
    char *final, *front;
    int size, popSize, cntBlock;
    const int capacity = BLOCK2_MAX_NUM * sizeof(block2);

    transferQueue() : buffer(NULL), final(NULL), front(NULL), size(0), popSize(0), cntBlock(0) {}

    transferQueue(char *ptr) : buffer(ptr), final(ptr), front(ptr), size(0), popSize(0), cntBlock(0) {}

    void set(char *ptr) {
        buffer = ptr;
        final = ptr;
        front = ptr;
        size = popSize = 0;
        cntBlock = 0;
    }

    bool empty() { return front == final; }

    void push(char *p, const int &dataSize) { //将块与块头数据转为char类型，返回此char区段头指针
        if (size + dataSize >= capacity) {
            std::cout << "transferQueue full" << std::endl;
            return;
        }
        memcpy(final, p, dataSize);
        cntBlock++;
        size += dataSize;
        final += dataSize;
    }

    queuePopMsg pop(const int &dataSize) {
        long long sz = std::min(dataSize, size - popSize);
        queuePopMsg ans = queuePopMsg(front, sz);
        front += sz;
        popSize += sz;
        return ans;
    }

    bool hasSize(const int &dataSize) {
        return size - popSize >= dataSize;
    }
} transferQueue1;

ThreadSafeQueue<long long> transferTSQ;
long long transferTSQ_cnt = 0;

struct compressArg {
    int compressSize, blockNum;
    char *src, *dst;
    int type;

    compressArg(char *s, char *d, const int &cS, const int &bN, const int &ty) : src(s), dst(d),
                                                                                                compressSize(cS),
                                                                                                blockNum(bN),
                                                                                                type(ty) {}
};

void compress_func(compressArg *arg) {
    if ((arg->src) != NULL) {
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
            block2Storage[arg->blockNum].blockSize = (arg->compressSize);
            memcpy((arg->dst), (arg->src), (arg->compressSize));
            if (block2Storage[arg->blockNum].blockSize > BLOCK2_MAX_SIZE) {
                std::cout << "compressed_data_size too big !" << std::endl;
                exit(1);
            }
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
        block2Storage[arg->blockNum].compressType = arg->type;
        pthread_mutex_lock(&transferTSQ_mutex);
        transferTSQ_cnt++;
        pthread_mutex_unlock(&transferTSQ_mutex);
        transferTSQ.push(arg->blockNum);
    }

    pthread_mutex_lock(&finish2_mutex);
    finish2_cnt += (arg->compressSize);
    pthread_mutex_unlock(&finish2_mutex);

    pthread_mutex_lock(&block2_mutex);
    block2_cnt--;
    pthread_mutex_lock(&add2_mutex);
    if (add2_end && (block2_cnt == 0)) {
        pthread_mutex_lock(&transferTSQ_mutex);
        compress_end = true;
        pthread_mutex_unlock(&transferTSQ_mutex);
        transferTSQ.push(-1);
    }
    pthread_mutex_unlock(&add2_mutex);
    pthread_mutex_unlock(&block2_mutex);

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
    int size;
    const int capacity = BLOCK3_MAX_NUM;

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

struct transferArg {
    int blockNum;

    transferArg(const int &bN) : blockNum(bN) {}
};

void transfer(transferArg *arg) {
    if ((arg->blockNum) != -1) {
        long long clientPort;
        for (clientPort = 0; clientPort < THREAD_MAX_NUM3; clientPort++) {
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

    pthread_mutex_lock(&finish3_mutex);
    finish3_cnt += block3Storage[arg->blockNum].blockSize;
    pthread_mutex_unlock(&finish3_mutex);

    pthread_mutex_lock(&block3_mutex);
    block3_cnt--;
    pthread_mutex_lock(&add3_mutex);
    if (add3_end && (block3_cnt == 0))transfer_end = true;
    pthread_mutex_unlock(&add3_mutex);
    pthread_mutex_unlock(&block3_mutex);

    delete arg;
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

void test3() {
    testBlock1Buffer = (block1 *) malloc(1ll * BLOCK1_MAX_NUM * sizeof(block1));
    testBlock2Buffer = (block2 *) malloc(1ll * BLOCK2_MAX_NUM * sizeof(block2));
    memBlock1Buffer = (block1 *) malloc(1ll * BLOCK1_MAX_NUM * sizeof(block1));
    memBlock2Buffer = (block2 *) malloc(1ll * BLOCK2_MAX_NUM * sizeof(block2));
    testDataBuffer = (char *) malloc(FILE_MAX_SIZE);
    testDataBuffer2 = (char *) malloc(FILE_MAX_SIZE);
    long long port = 8080;
    auto client = httplib::Client(ServerIP, port);
    long long sz = block3Storage.size;
    std::cout << "block3 num: " << sz << std::endl;
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
    std::cout << "transferQueue size: " << transferQueue1.size << std::endl;
    std::cout << "get size: " << sz << std::endl;
    long long cnt=0;
    frameHeader *header;
    p=testDataBuffer2;
    while(p - testDataBuffer2 < sz) {
        header = (frameHeader *)p;
        p += sizeof(frameHeader);
        memcpy(memBlock2Buffer + cnt, p, header->dataSize);
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

    std::cout << "block2 num: " << cnt << std::endl;
    for (long long i = 0; i < cnt; i++) {
        testBlock2Buffer[memBlock2Buffer[i].blockNum] = memBlock2Buffer[i];
    }
    for (long long i = 0, compressType; i < cnt; i++) {
        compressType = testBlock2Buffer[i].compressType;
        if (compressType == 0) {
            long long decompressed_size = LZ4_decompress_safe(testBlock2Buffer[i].buffer, p, testBlock2Buffer[i].blockSize,
                                                        block2Size);
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
            long long decompressed_size = testBlock2Buffer[i].blockSize;
            memcpy(p, testBlock2Buffer[i].buffer, decompressed_size);
            testBlock2Buffer[i].blockNum = i;
            testBlock2Buffer[i].blockSize = decompressed_size;
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
    std::cout << "depressed size: " << sz << std::endl;
    std::cout << "compressQueue size: " << compressQueue1.size << std::endl;
    memcpy(memBlock1Buffer, testDataBuffer, sz);
    if (sz % sizeof(block1)) {
        std::cout << "depressed size does not match the block size ! " << std::endl;
        exit(1);
    }

    sz = sz / sizeof(block1);
    for (long long i = 0; i < sz; i++) {
        testBlock1Buffer[memBlock1Buffer[i].blockNum] = memBlock1Buffer[i];
    }
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

    free(memBlock1Buffer);
    free(memBlock2Buffer);
    free(testBlock1Buffer);
    free(testBlock2Buffer);
    free(testDataBuffer);
    free(testDataBuffer2);
}

void allocate_memory() {
    fileStorage_ptr = (fileMsg *) malloc(1ll * FILE_MAX_NUM * sizeof(fileMsg));
    fileStorage.set(fileStorage_ptr);
    block1Storage_ptr = (block1 *) malloc(1ll * BLOCK1_MAX_NUM * sizeof(block1));
    block1Storage.set(block1Storage_ptr);
    compressQueue1_ptr = (char *) malloc((1ll * BLOCK1_MAX_NUM * sizeof(block1)) * sizeof(char));
    compressQueue1.set(compressQueue1_ptr);
    compressedBuffer_ptr = (char *) malloc((1ll * BLOCK2_MAX_NUM * BLOCK2_MAX_SIZE) * sizeof(char));
    block2Storage_ptr = (block2 *) malloc(1ll * BLOCK2_MAX_NUM * sizeof(block2));
    block2Storage.set(block2Storage_ptr);
    transferQueue1_ptr = (char *) malloc((1ll * BLOCK2_MAX_NUM * sizeof(block2)) * sizeof(char));
    transferQueue1.set(transferQueue1_ptr);
    block3Storage_ptr = (block3 *) malloc(1ll * BLOCK3_MAX_NUM * sizeof(block3));
    block3Storage.set(block3Storage_ptr);
}

void free_memory() {
    free(fileStorage_ptr);
    free(block1Storage_ptr);
    free(compressQueue1_ptr);
    free(compressedBuffer_ptr);
    free(block2Storage_ptr);
    free(transferQueue1_ptr);
    free(block3Storage_ptr);
}

void run_compressQueue1(threadpool *thpool) {
    queuePopMsg now;
    long long nowBlock;
    threadpool *nowThpool;
    while (true) {
        pthread_mutex_lock(&compressTSQ_mutex);
        if (read_end && compressTSQ_cnt == 0) {
            pthread_mutex_unlock(&compressTSQ_mutex);
            break;
        } else {
            pthread_mutex_unlock(&compressTSQ_mutex);
        }
        nowBlock = compressTSQ.pop();
        if (nowBlock == -1) continue;
        pthread_mutex_lock(&compressTSQ_mutex);
        compressTSQ_cnt--;
        pthread_mutex_unlock(&compressTSQ_mutex);
        compressQueue1.push((char *)(&block1Storage[nowBlock]), sizeof(block1));
        while (compressQueue1.hasSize(block2Size)) {
            now = compressQueue1.pop(block2Size);
            char *src_ptr = now.ptr;
            long long blockNum = block2Storage.size;
            char *dst_ptr = compressedBuffer_ptr + blockNum * BLOCK2_MAX_SIZE;
            block2Storage.push();
            while (true) {
                pthread_mutex_lock(&control2_mutex[0]);
                if (control2[0] && control2[2]>0) {
                    nowThpool = thpool + control2[0] - 1;
                    control2[2] -= now.popSize;
                    pthread_mutex_unlock(&control2_mutex[0]);
                    break;
                }
                pthread_mutex_unlock(&control2_mutex[0]);
            }
            pthread_mutex_lock(&control2_mutex[1]);
            compressArg *arg = new compressArg(src_ptr, dst_ptr,
                                               now.popSize, blockNum, control2[1]);
            pthread_mutex_unlock(&control2_mutex[1]);
            pthread_mutex_lock(&block2_mutex);
            block2_cnt++;
            pthread_mutex_unlock(&block2_mutex);
            thpool_add_work((*nowThpool), reinterpret_cast<void (*)(void *)>(compress_func), arg);
        }
    }
    now = compressQueue1.pop(block2Size);
    while (now.popSize != 0) {
        char *src_ptr = now.ptr;
        long long blockNum = block2Storage.size;
        char *dst_ptr = compressedBuffer_ptr + blockNum * BLOCK2_MAX_SIZE;
        block2Storage.push();
        while (true) {
            pthread_mutex_lock(&control2_mutex[0]);
            if (control2[0] && control2[2]>0) {
                nowThpool = thpool + control2[0] - 1;
                control2[2] -= now.popSize;
                pthread_mutex_unlock(&control2_mutex[0]);
                break;
            }
            pthread_mutex_unlock(&control2_mutex[0]);
        }
        pthread_mutex_lock(&control2_mutex[1]);
        compressArg *arg = new compressArg(src_ptr, dst_ptr,
                                           now.popSize, blockNum, control2[1]);
        pthread_mutex_unlock(&control2_mutex[1]);
        pthread_mutex_lock(&block2_mutex);
        block2_cnt++;
        pthread_mutex_unlock(&block2_mutex);
        thpool_add_work((*nowThpool), reinterpret_cast<void (*)(void *)>(compress_func), arg);
        now = compressQueue1.pop(block2Size);
    }
    while (true) {
        pthread_mutex_lock(&control2_mutex[0]);
        if (control2[0] && control2[2]>0) {
            nowThpool = thpool + control2[0] - 1;
            pthread_mutex_unlock(&control2_mutex[0]);
            break;
        }
        pthread_mutex_unlock(&control2_mutex[0]);
    }
    pthread_mutex_lock(&add2_mutex);
    add2_end = true;
    pthread_mutex_unlock(&add2_mutex);
    compressArg *arg = new compressArg(NULL, NULL, -1, -1, -1);
    pthread_mutex_lock(&block2_mutex);
    block2_cnt++;
    pthread_mutex_unlock(&block2_mutex);
    thpool_add_work((*nowThpool), reinterpret_cast<void (*)(void *)>(compress_func), arg);
}

void run_transferQueue1(threadpool *thpool) {
    queuePopMsg now;
    long long nowBlock;
    frameHeader header;
    threadpool *nowThpool;
    while (true) {
        pthread_mutex_lock(&transferTSQ_mutex);
        if (compress_end && transferTSQ_cnt == 0) {
            pthread_mutex_unlock(&transferTSQ_mutex);
            break;
        } else {
            pthread_mutex_unlock(&transferTSQ_mutex);
        }
        nowBlock = transferTSQ.pop();
        if (nowBlock == -1) continue;
        pthread_mutex_lock(&transferTSQ_mutex);
        transferTSQ_cnt--;
        pthread_mutex_unlock(&transferTSQ_mutex);
        header = block2Storage[nowBlock].get_header();
        transferQueue1.push((char *)(&header), sizeof(frameHeader));
        transferQueue1.push((char *)(&block2Storage[nowBlock]), header.dataSize);
        while (transferQueue1.hasSize(block3Size)) {
            now = transferQueue1.pop(block3Size);
            char *src = now.ptr;
            char *p = block3Storage.push(now.popSize);
            memcpy(p, src, now.popSize);
            transferArg *arg = new transferArg(block3Storage.size - 1);
            while (true) {
                pthread_mutex_lock(&control3_mutex);
                if (control3[0] && control3[1]>0) {
                    nowThpool = thpool + control3[0] - 1;
                    control3[1] -= block3Storage[arg->blockNum].blockSize;
                    pthread_mutex_unlock(&control3_mutex);
                    break;
                }
                pthread_mutex_unlock(&control3_mutex);
            }
            pthread_mutex_lock(&block3_mutex);
            block3_cnt++;
            pthread_mutex_unlock(&block3_mutex);
            thpool_add_work((*nowThpool), reinterpret_cast<void (*)(void *)>(transfer), arg);
        }
    }
    now = transferQueue1.pop(block3Size);
    while (now.popSize != 0) {
        char *src = now.ptr;
        char *p = block3Storage.push(now.popSize);
        memcpy(p, src, now.popSize);
        transferArg *arg = new transferArg(block3Storage.size - 1);
        while (true) {
            pthread_mutex_lock(&control3_mutex);
            if (control3[0] && control3[1]>0) {
                nowThpool = thpool + control3[0] - 1;
                control3[1] -= block3Storage[arg->blockNum].blockSize;
                pthread_mutex_unlock(&control3_mutex);
                break;
            }
            pthread_mutex_unlock(&control3_mutex);
        }
        pthread_mutex_lock(&block3_mutex);
        block3_cnt++;
        pthread_mutex_unlock(&block3_mutex);
        thpool_add_work((*nowThpool), reinterpret_cast<void (*)(void *)>(transfer), arg);
        now = transferQueue1.pop(block3Size);
    }
    while (true) {
        pthread_mutex_lock(&control3_mutex);
        if (control3[0] && control3[1]>0) {
            nowThpool = thpool + control3[0] - 1;
            pthread_mutex_unlock(&control3_mutex);
            break;
        }
        pthread_mutex_unlock(&control3_mutex);
    }
    pthread_mutex_lock(&add3_mutex);
    add3_end = true;
    pthread_mutex_unlock(&add3_mutex);
    transferArg *arg = new transferArg(-1);
    pthread_mutex_lock(&block3_mutex);
    block3_cnt++;
    pthread_mutex_unlock(&block3_mutex);
    thpool_add_work((*nowThpool), reinterpret_cast<void (*)(void *)>(transfer), arg);
}

void run_thpoolQueues0(threadpool *thpool) {
    trave_dir(std::string(FOLDER_IN).c_str(), thpool);
}

void setControl1(const long long &val0, const long long &val1) {
    #ifdef monitor_show
    std::cout << "setControl1: " << val0 << ' ' << val1 << std::endl;
    #endif
    if (val0 >= 0 && val1 >= 0) {
        pthread_mutex_lock(&control1_mutex);
        control1[0] = val0;
        control1[1] += val1;
        pthread_mutex_unlock(&control1_mutex);
    }
}

void setControl2(const long long &val0, const long long &val1, const long long &val2) {
    #ifdef monitor_show
    std::cout << "setControl2: " << val0 << ' ' << val1 << ' ' << val2 << std::endl;
    #endif
    if (val0 >= 0 && val2 >= 0) {
        pthread_mutex_lock(&control2_mutex[0]);
        control2[0] = val0;
        control2[2] += val2;
        pthread_mutex_unlock(&control2_mutex[0]);
    }
    if (val1 >= 0) {
        pthread_mutex_lock(&control2_mutex[1]);
        control2[1] = val1;
        pthread_mutex_unlock(&control2_mutex[1]);
    }
}

void setControl3(const long long &val0, const long long &val1) {
    #ifdef monitor_show
    std::cout << "setControl3: " << val0 << ' ' << val1 << std::endl;
    #endif
    if (val0 >= 0 && val1 >= 0) {
        pthread_mutex_lock(&control3_mutex);
        control3[0] = val0;
        control3[1] += val1;
        pthread_mutex_unlock(&control3_mutex);
    }
}

struct controlMethod{
    struct period{
        long long control1[2], control2[3], control3[2];
    }p[3];
    void put(int _p,int op){
        if(op==1){
            setControl1(p[_p].control1[0], p[_p].control1[1]);
        }else if(op==2){
            setControl2(p[_p].control2[0], p[_p].control2[1], p[_p].control2[2]);
        }else if(op==3){
            setControl3(p[_p].control3[0], p[_p].control3[1]);
        }
    }
    void init(int _p){
        put(_p, 1);
        put(_p, 2);
        put(_p, 3);
    }
};

struct Model{
    double D, V1, V3, C[4][4], alpha[4];
    void init(){
        D = 64.0 * 1024 * 1024;
        V1 = 37.5 * 1024 * 1024;
        V3 = 1 * 1024 * 1024;
        C[0][0] = 0;
        C[0][1] = 96.3 * 1024 * 1024;
        C[0][2] = 159.6 * 1024 * 1024;
        C[0][3] = 199.0 * 1024 * 1024;
        alpha[0] = 1.9;
        C[1][0] = 0;
        C[1][1] = 6.6 * 1024 * 1024;
        C[1][2] = 12.7 * 1024 * 1024;
        C[1][3] = 16.9 * 1024 * 1024;
        alpha[1] = 3.1;
        C[2][0] = 0;
        C[2][1] = 4.2 * 1024 * 1024;
        C[2][2] = 7.6 * 1024 * 1024;
        C[2][3] = 9.8 * 1024 * 1024;
        alpha[2] = 3.6;
        C[3][0] = 0;
        C[3][1] = 88.1 * 1024 * 1024;
        C[3][2] = 85.6 * 1024 * 1024;
        C[3][3] = 85.2 * 1024 * 1024;
        alpha[3] = 1.0;
    }
    controlMethod solveCT(const int &cT, double &T){
        controlMethod cM;
        double t1, t2, t3, _V3, x, _t;
        _V3 = alpha[cT] * V3;
        _t = D / _V3;
        t1 = D / V1;
        t2 = D * (V1 - C[cT][1]) / (V1 * C[cT][2]);
        if(cT == 0 || cT == 3 || t1 + t2 < _t){
            cM.p[0].control1[0] = 1;
            cM.p[0].control2[0] = 1;
            cM.p[0].control3[0] = 1;
            cM.p[0].control1[1] = t1 * V1;
            cM.p[0].control2[2] = t1 * C[cT][1];
            cM.p[0].control3[1] = t1 * _V3;

            cM.p[1].control1[0] = 0;
            cM.p[1].control2[0] = 2;
            cM.p[1].control3[0] = 1;
            cM.p[1].control1[1] = 1;
            cM.p[1].control2[2] = t2 * C[cT][2];
            cM.p[1].control3[1] = t2 * _V3;

            cM.p[2].control1[0] = 1;
            cM.p[2].control2[0] = 1;
            cM.p[2].control3[0] = 1;
            cM.p[2].control1[1] = 0;
            cM.p[2].control2[2] = 0;
            cM.p[2].control3[1] = 0;

            T = std::max(t1 + t2, _t);
        }else if(_t <= t2){
            x = D - D * (C[cT][2] / V1) - _t * C[cT][2];
            t1 = D / V1;
            t2 = x / C[cT][3];
            t3 = _t;

            cM.p[0].control1[0] = 1;
            cM.p[0].control2[0] = 2;
            cM.p[0].control3[0] = 0;
            cM.p[0].control1[1] = t1 * V1;
            cM.p[0].control2[2] = t1 * C[cT][2];
            cM.p[0].control3[1] = 1;

            cM.p[1].control1[0] = 0;
            cM.p[1].control2[0] = 3;
            cM.p[1].control3[0] = 0;
            cM.p[1].control1[1] = 1;
            cM.p[1].control2[2] = t2 * C[cT][3];
            cM.p[1].control3[1] = 1;

            cM.p[2].control1[0] = 0;
            cM.p[2].control2[0] = 2;
            cM.p[2].control3[0] = 1;
            cM.p[2].control1[1] = 1;
            cM.p[2].control2[2] = t3 * C[cT][2];
            cM.p[2].control3[1] = t3 * _V3;

            T = t1 + t2 + t3;
        }else if(t2 < _t && _t <= t1 + t2){
            x = (D - (D / V1 + _t) * C[cT][2]) / (C[cT][1] - C[cT][2] * 2);
            t1 = D / V1 - x;
            t2 = x;
            t3 = _t - x;

            cM.p[0].control1[0] = 1;
            cM.p[0].control2[0] = 2;
            cM.p[0].control3[0] = 0;
            cM.p[0].control1[1] = t1 * V1;
            cM.p[0].control2[2] = t1 * C[cT][2];
            cM.p[0].control3[1] = 1;

            cM.p[1].control1[0] = 1;
            cM.p[1].control2[0] = 1;
            cM.p[1].control3[0] = 1;
            cM.p[1].control1[1] = t2 * V1;
            cM.p[1].control2[2] = t2 * C[cT][1];
            cM.p[1].control3[1] = t2 * _V3;

            cM.p[2].control1[0] = 0;
            cM.p[2].control2[0] = 2;
            cM.p[2].control3[0] = 1;
            cM.p[2].control1[1] = 1;
            cM.p[2].control2[2] = t3 * C[cT][2];
            cM.p[2].control3[1] = t3 * _V3;

            T = t1 + t2 + t3;
        }
        cM.p[0].control2[1] = cT;
        cM.p[1].control2[1] = cT;
        cM.p[2].control2[1] = cT;
        return cM;
    }
    controlMethod solve(){
        controlMethod result, nxt;
        double mivT, nxtT;
        result = solveCT(3, mivT);
        for(int cT = 0; cT < 3;cT ++){
            nxt = solveCT(cT, nxtT);
            if(nxtT < mivT){
                mivT = nxtT;
                result = nxt;
            }
        }
        std::cout << "expected: " << D/mivT/1024/1024 << std::endl;
        return result;
    }
}model;

bool finish(){
    bool res = false;
    pthread_mutex_lock(&control1_mutex);
    pthread_mutex_lock(&add1_mutex);
    if (control1[1]<=0 || (control1[0] && add1_end)) res = true;
    pthread_mutex_unlock(&add1_mutex);
    pthread_mutex_unlock(&control1_mutex);
    if(res)return true;

    pthread_mutex_lock(&control2_mutex[0]);
    pthread_mutex_lock(&add2_mutex);
    if (control2[2]<=0 || (control2[0] && add2_end)) res = true;
    pthread_mutex_unlock(&add2_mutex);
    pthread_mutex_unlock(&control2_mutex[0]);
    if(res)return true;

    pthread_mutex_lock(&control3_mutex);
    pthread_mutex_lock(&add3_mutex);
    if (control3[1]<=0 || (control3[0] && add3_end)) res = true;
    pthread_mutex_unlock(&add3_mutex);
    pthread_mutex_unlock(&control3_mutex);

    return res;
}

void monitor() {
//    auto method = model.solve();
    controlMethod method;
    method.p[0].control1[0] = 1;
    method.p[0].control1[1] = 500.0 * 1024 * 1024;
    method.p[0].control2[0] = 2;
    method.p[0].control2[1] = 2;
    method.p[0].control2[2] = 500.0 * 1024 * 1024;
    method.p[0].control3[0] = 1;
    method.p[0].control3[1] = 500.0 * 1024 * 1024;
    method.p[1]=method.p[2]=method.p[0];
    long long flag = 0;
    method.init(0);
    long long preFinish1 = 0, preFinish2 = 0, preFinish3 = 0;
    while (1) {
        usleep(TIMESPAN);
        //每隔TIMESPAN微妙

        pthread_mutex_lock(&add3_mutex);
        if (add3_end) {
            pthread_mutex_unlock(&add3_mutex);
            std::cout << "Add all task over !" << std::endl;
            break;
        }
        pthread_mutex_unlock(&add3_mutex);

        /*
        #ifdef monitor_show
        std::cout << "Speed Measurement" << std::endl;
        #endif
        pthread_mutex_lock(&finish1_mutex);
        #ifdef monitor_show
        std::cout << "Module1 speed : " << std::fixed << std::setprecision(2)
                  << 1.0 * (finish1_cnt - preFinish1) / (TIMESPAN / 1000) << "byte/ms" << std::endl;
        #endif
        preFinish1 = finish1_cnt;
        pthread_mutex_unlock(&finish1_mutex);
        pthread_mutex_lock(&finish2_mutex);
        #ifdef monitor_show
        std::cout << "Module2 speed : " << std::fixed << std::setprecision(2)
                  << 1.0 * (finish2_cnt - preFinish2) / (TIMESPAN / 1000) << "byte/ms" << std::endl;
        #endif
        preFinish2 = finish2_cnt;
        pthread_mutex_unlock(&finish2_mutex);
        pthread_mutex_lock(&finish3_mutex);
        #ifdef monitor_show
        std::cout << "Module3 speed : " << std::fixed << std::setprecision(2)
                  << 1.0 * (finish3_cnt - preFinish3) / (TIMESPAN / 1000) << "byte/ms" << std::endl;
        #endif
        preFinish3 = finish3_cnt;
        pthread_mutex_unlock(&finish3_mutex);

        #ifdef monitor_show
        std::cout << "Implement Control Algorithm" << std::endl;
        #endif
         */

        if(finish()){
            if(flag == 0){
                method.init(1);
                flag = 1;
            }else if(flag == 1){
                method.init(2);
                flag = 2;
            }else if(flag == 2){
                method.init(0);
                flag= 0;
            }
        }
    }
}

long long toNumber(char *ch){
    long long num=0,len=std::strlen(ch);
    for(int i=0;i<len;i++){
        num*=10;
        num+=ch[i]-'0';
    }
    return num;
}

int main() {
    model.init();

    allocate_memory();
    thpoolQueues0 = thpool_init(QUEUE_NUM0);
    thpoolQueues1 = thpool_init(QUEUE_NUM1);
    thpoolQueues2 = thpool_init(QUEUE_NUM2);
    thpoolMonitor = thpool_init(1);
    for (long long i = 0; i < THREAD_MAX_NUM1; i++) {
        thpoolInput[i] = thpool_init(i + 1);
    }
    for (long long i = 0; i < THREAD_MAX_NUM2; i++) {
        thpoolCompress[i] = thpool_init(i + 1);
    }
    for (long long i = 0; i < THREAD_MAX_NUM3; i++) {
        thpoolTransfer[i] = thpool_init(i + 1);
    }
    for (long long i = 0; i < THREAD_MAX_NUM3; i++) {
        pthread_mutex_init(&port_mutex[i], NULL);
        clientSet[i] = new httplib::Client(ServerIP, 8080 + i);
        clientPort_flag[i] = false;
    }
    pthread_mutex_init(&block1_mutex, NULL);
    pthread_mutex_init(&block2_mutex, NULL);
    pthread_mutex_init(&block3_mutex, NULL);
    pthread_mutex_init(&finish1_mutex, NULL);
    pthread_mutex_init(&finish2_mutex, NULL);
    pthread_mutex_init(&finish3_mutex, NULL);
    pthread_mutex_init(&add1_mutex, NULL);
    pthread_mutex_init(&add2_mutex, NULL);
    pthread_mutex_init(&add3_mutex, NULL);
    pthread_mutex_init(&compressTSQ_mutex, NULL);
    pthread_mutex_init(&transferTSQ_mutex, NULL);
    pthread_mutex_init(&control1_mutex, NULL);
    pthread_mutex_init(&control2_mutex[0], NULL);
    pthread_mutex_init(&control2_mutex[1], NULL);
    pthread_mutex_init(&control3_mutex, NULL);
    std::cout << std::endl;

    std::cout << "step0: start monitor program" << std::endl;
    thpool_add_work(thpoolMonitor, reinterpret_cast<void (*)(void *)>(monitor), NULL);

    std::cout << "step1: start compressQueue1 include compress_func" << std::endl;
    thpool_add_work(thpoolQueues1, reinterpret_cast<void (*)(void *)>(run_compressQueue1), thpoolCompress);

    std::cout << "step2: start transferQueue1 include transfer" << std::endl;
    thpool_add_work(thpoolQueues2, reinterpret_cast<void (*)(void *)>(run_transferQueue1), thpoolTransfer);

    std::cout << "step3: begin trave_dir and read files" << std::endl;
    thpool_add_work(thpoolQueues0, reinterpret_cast<void (*)(void *)>(run_thpoolQueues0), thpoolInput);

    std::cout<<"============================begin============================"<<std::endl;
    std::thread cpu1(cpu_begin);
    pthread_t id1 = cpu1.native_handle();
    cpu1.detach();
    auto start_time = std::chrono::high_resolution_clock::now();

//能否用add3_end唤醒thpoolMonitor
    thpool_wait(thpoolMonitor);
    thpool_wait(thpoolQueues0);
    thpool_wait(thpoolQueues1);
    thpool_wait(thpoolQueues2);
    for (long long i = 0; i < THREAD_MAX_NUM1; i++) {
        thpool_wait(thpoolInput[i]);
    }
    for (long long i = 0; i < THREAD_MAX_NUM2; i++) {
        thpool_wait(thpoolCompress[i]);
    }
    for (long long i = 0; i < THREAD_MAX_NUM3; i++) {
        thpool_wait(thpoolTransfer[i]);
    }

//    std::freopen("data.out","a",stdout);
    std::cout << "=============================begin=============================" << std::endl;
    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_time = end_time - start_time;
    std::cout << "Running time: " << std::fixed << std::setprecision(1) << elapsed_time.count() * 1000 << " ms" << std::endl;
    std::cout << "Bandwidth: " << std::fixed << std::setprecision(1) << 134.5 / elapsed_time.count() << " MB/s" << std::endl;
    std::cout << "Average CPU Usage: " << std::fixed << std::setprecision(1) << cpu_end(id1, elapsed_time.count() * 1000) << std::endl;
    std::cout << "=============================end=============================" << std::endl;
    std::fclose(stdout);

//    std::freopen("/dev/console", "w", stdout);
    std::cout << "=============================end=============================" << std::endl;

    std::cout << "step all over" << std::endl;

//    test3();

    thpool_destroy(thpoolMonitor);
    for (long long i = 0; i < THREAD_MAX_NUM1; i++) {
        thpool_destroy(thpoolInput[i]);
    }
    for (long long i = 0; i < THREAD_MAX_NUM2; i++) {
        thpool_destroy(thpoolCompress[i]);
    }
    for (long long i = 0; i < THREAD_MAX_NUM3; i++) {
        thpool_destroy(thpoolTransfer[i]);
    }
    thpool_destroy(thpoolQueues0);
    thpool_destroy(thpoolQueues1);
    thpool_destroy(thpoolQueues2);
    for (long long i = 0; i < THREAD_MAX_NUM3; i++) {
        pthread_mutex_destroy(&port_mutex[i]);
    }
    pthread_mutex_destroy(&block1_mutex);
    pthread_mutex_destroy(&block2_mutex);
    pthread_mutex_destroy(&block3_mutex);
    pthread_mutex_destroy(&finish1_mutex);
    pthread_mutex_destroy(&finish2_mutex);
    pthread_mutex_destroy(&finish3_mutex);
    pthread_mutex_destroy(&add1_mutex);
    pthread_mutex_destroy(&add2_mutex);
    pthread_mutex_destroy(&add3_mutex);
    pthread_mutex_destroy(&compressTSQ_mutex);
    pthread_mutex_destroy(&transferTSQ_mutex);
    pthread_mutex_destroy(&control1_mutex);
    pthread_mutex_destroy(&control2_mutex[0]);
    pthread_mutex_destroy(&control2_mutex[1]);
    pthread_mutex_destroy(&control3_mutex);
    free_memory();

    std::cout << "over!" << std::endl;
    return 0;
}
