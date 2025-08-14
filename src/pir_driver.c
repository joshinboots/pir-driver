// PIR driver: treat any CHANGE in EVENT_REG (0x03) as a motion event.
// Motion=true while the value has changed within WINDOW_MS, else false.

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>        
#include <stdint.h>   
#include <stdbool.h> 
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>    
#include <sys/stat.h>     
#include <signal.h>                  
#include <string.h>     
#include <time.h>                                                   

#define DEFAULT_BUS   5                  
#define DEFAULT_ADDR  0x12                        
#define EVENT_REG     0x03
#define FIFO_PATH     "/tmp/pir_fifo"
#define POLL_MS       50                                    
#define WINDOW_MS     2000   // show MOTION for 2s after last change
 
static volatile sig_atomic_t runflag = 1;                                                          
static void on_sig(int s){ (void)s; runflag = 0; }
                                                                          
static long now_ms(void){
    struct timespec ts;
#ifdef CLOCK_MONOTONIC
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return (long)(ts.tv_sec*1000LL + ts.tv_nsec/1000000LL);
}

static void msleep(int ms){ struct timespec ts={ms/1000,(ms%1000)*1000000L}; nanosleep(&ts,NULL); }
                                          
static int i2c_read_byte(int fd, uint8_t addr, uint8_t reg, uint8_t *val){
    struct i2c_msg msgs[2];
    uint8_t wbuf[1]={reg}, rbuf[1]={0};
    msgs[0]=(struct i2c_msg){.addr=addr,.flags=0,.len=1,.buf=wbuf};
    msgs[1]=(struct i2c_msg){.addr=addr,.flags=I2C_M_RD,.len=1,.buf=rbuf};
    struct i2c_rdwr_ioctl_data x={.msgs=msgs,.nmsgs=2};
    if(ioctl(fd,I2C_RDWR,&x)<0) return -1;                        
    *val=rbuf[0]; return 0;                                                           
}    

int main(int argc, char **argv){                  
    int bus=DEFAULT_BUS, addr=DEFAULT_ADDR;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"-b") && i+1<argc) bus=atoi(argv[++i]);
        else if(!strcmp(argv[i],"-a") && i+1<argc) addr=(int)strtol(argv[++i],NULL,0);
    }
                                                              
    signal(SIGINT,on_sig); signal(SIGTERM,on_sig);
                                                             
    mkfifo(FIFO_PATH,0666);
    int fdfifo=open(FIFO_PATH,O_RDWR|O_NONBLOCK);
    if(fdfifo<0){perror("open fifo"); return 1;}
                                                            
    char dev[32]; snprintf(dev,sizeof(dev),"/dev/i2c-%d",bus);
    int fdi2c=open(dev,O_RDWR);
    if(fdi2c<0){perror("open i2c"); close(fdfifo); return 1;}
                                                                
    uint8_t prev=0; bool have_prev=false;             
    long last_change = -1000000;                                                   
    bool motion=false, last_reported=false; bool first=true;
                                                      
    while(runflag){
        uint8_t st=0;                      
        if(i2c_read_byte(fdi2c,(uint8_t)addr,EVENT_REG,&st)==0){          
            if(!have_prev){ prev=st; have_prev=true; }
            else if(st!=prev){ prev=st; last_change=now_ms(); } // CHANGE → event  
        }               
        motion = (now_ms() - last_change) < WINDOW_MS;
                                
        if(first || motion!=last_reported){
            dprintf(fdfifo,"{\"motion\": %s}\n", motion ? "true":"false");
            last_reported=motion; first=false;
        }                                                                          
        msleep(POLL_MS);
    }                                                 
    close(fdi2c); close(fdfifo);
    return 0;                              
}                                                                         
             