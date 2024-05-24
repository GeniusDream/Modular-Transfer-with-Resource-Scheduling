//
// Created by THINK on 2023/8/30.
//

#ifndef TEST_CPUTEST_H
#define TEST_CPUTEST_H
#include <thread>
#include <pthread.h>
#include <vector>
#include <iostream>
#include <fstream>

#define TIME_INTERVAL 0.1

void cpu_begin(){
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    std::ofstream file_writer("/home/c508/cpu.log",std::ios_base::out);
    system("sudo bash /home/c508/testcpu.sh");
}

double cpu_end(pthread_t id,int time){
    system("sudo killall bash");
    std::vector<double>data;
    std::cin.clear();
    freopen("/home/c508/cpu.log","r",stdin);
    double x,sum=0;
    int cnt=0;
    while(std::cin>>x){
        sum+=x;cnt++;
        data.push_back(x);
    }
    std::cin.clear();
    freopen("/dev/tty", "r", stdin);
    if(cnt>2){
        cnt-=2;
        time-=2*(TIME_INTERVAL*1000);
        sum-=data[0]+data[data.size()-1];
    }
    int res=(time-cnt*(TIME_INTERVAL*1000))/(TIME_INTERVAL*1000)-2;
    if(res>0){
        sum+=res*100;
        cnt+=res;
    }
    return sum/cnt;
}

#endif //TEST_CPUTEST_H
