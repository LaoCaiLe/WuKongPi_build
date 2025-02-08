#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <linux/input.h>
#include <mqueue.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <syslog.h>
#define QUEUE_NAME "/msg_queue"
#define MSG_SIZE sizeof(struct input_event)

#define ENCODER_KEY_EVENT "/dev/input/event0"
#define GPIO_KEY_EVENT "/dev/input/event1"

#define RUN_MONITOR_PROCESS_CMD "/root/demo"
#define EXIT_MONITOR_PROCESS_CMD "kill $(pidof -s /root/demo)"
#define IS_XFCE_RUNNING_CMD "pgrep xfwm4"
#define ENTER_XFCE_DESKTOP_CMD "systemctl start lightdm"
#define EXIT_XFCE_DESKTOP_CMD "systemctl stop lightdm"
#define GREEN_LED_HEARTBEAT_CMD "echo 1 > /sys/class/leds/green_led/brightness; echo heartbeat > /sys/class/leds/green_led/trigger"
#define GREEN_LED_DEFAULT_ON_CMD "echo 1 > /sys/class/leds/green_led/brightness; echo default-on  > /sys/class/leds/green_led/trigger"
#define GREEN_LED_OFF_CMD "echo 0 > /sys/class/leds/green_led/brightness"
#define RED_LED_HEARTBEAT_CMD "echo 1 > /sys/class/leds/red_led/brightness; echo heartbeat > /sys/class/leds/red_led/trigger"
#define RED_LED_DEFAULT_ON_CMD "echo 1 > /sys/class/leds/red_led/brightness; echo default-on  > /sys/class/leds/red_led/trigger"
#define RED_LED_OFF_CMD "echo 0 > /sys/class/leds/red_led/brightness"
#define TTY_CURSOR_OFF_CMD "setterm -cursor off > /dev/tty1"
#define TTY_CURSOR_ON_CMD "setterm -cursor on > /dev/tty1"

typedef enum key_event_e{
    KEY_EVENT_UNKNOW,
    KEY_EVENT_ENCODER_FRONT,
    KEY_EVENT_ENCODER_BACK,
    KEY_EVENT_BTN_PRESS,
    KEY_EVENT_BTN_PRESS_UP,
    KEY_EVENT_BTN_HOLD,
    KEY_EVENT_BTN_HOLD_UP
}key_event_t;

typedef struct
{
    int key_encoder_fd;
    int key_gpio_fd;
    mqd_t qfd;
    pthread_t key_recv_thread;
    pthread_t key_deal_thread;
    key_event_t key_event;
    key_event_t last_key_event;

} key_task_t;
static key_task_t s_key_task_t = {0};
static bool is_red_led_on = true;
void* key_thread(void * arg)
{
    int i = 0;
    int n,ret;
    struct epoll_event ep_event, ep[64];
    struct input_event key_event;
    int epfd = epoll_create(64);
    ep_event.data.fd = s_key_task_t.key_encoder_fd;
    ep_event.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, s_key_task_t.key_encoder_fd, &ep_event);

    ep_event.data.fd = s_key_task_t.key_gpio_fd;
    ep_event.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, s_key_task_t.key_gpio_fd, &ep_event);

    while(1)
    {
        n = epoll_wait(epfd, ep, 64, -1);
        for (i = 0; i < n; i++)
        { 
            if(ep[i].events == EPOLLIN)
            {
                ret = read(ep[i].data.fd, &key_event, sizeof(struct input_event));
                if(ret == -1)
                {
                    syslog(LOG_ERR,"read the fd %d error because %s\n", ep[i].data.fd, strerror(errno));
                }
                else
                {
                    ret = mq_send(s_key_task_t.qfd, (char *)&key_event, sizeof(struct input_event), 0);
                    syslog(LOG_INFO,"send input key type[%d],code[%d],value[%d],recv_time[%lu]\n", key_event.type, key_event.code, key_event.value, key_event.time.tv_sec * 1000 + key_event.time.tv_usec/1000);
                    if(ret == -1)
                    {
                        syslog(LOG_ERR,"send the mqueue %d error because %s\n", s_key_task_t.qfd, strerror(errno));
                    }
                }
            }
        }
        usleep(10);
    }
    return NULL;
}

static bool check_xfce_running(void)
{
    char buffer[1024];
    bool ret;
    FILE *fp = popen(IS_XFCE_RUNNING_CMD, "r");
    if (fp == NULL) {
        syslog(LOG_ERR,"open the command %s error because %s\n", IS_XFCE_RUNNING_CMD, strerror(errno));
        return false;
    }

    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        ret = false;
    } else {
        ret = true;
    }

    pclose(fp);
    return ret;
}

static bool get_key_event(void)
{
    int ret;
    struct input_event event;
    uint32_t recv_time_ms;

    static uint32_t last_press_time = 0;

    memset(&event, 0x00, sizeof(event));
    ret = mq_receive(s_key_task_t.qfd, (char *)&event, sizeof(struct input_event), 0);
    if(ret == -1)
    {
        syslog(LOG_ERR,"receive the mqueue %d error because %s\n", s_key_task_t.qfd, strerror(errno));
    }
    recv_time_ms = event.time.tv_sec * 1000 + event.time.tv_usec / 1000;
    syslog(LOG_DEBUG,"recv input key type[%d],code[%d],value[%d],recv_time[%u]\n", event.type, event.code, event.value, recv_time_ms);
    if(event.type == EV_REL)
    {
        if(event.value==1)
            s_key_task_t.key_event = KEY_EVENT_ENCODER_FRONT;
        else if(event.value==-1)
            s_key_task_t.key_event = KEY_EVENT_ENCODER_BACK;
        else
            s_key_task_t.key_event = KEY_EVENT_UNKNOW;
    }
    else if(event.type == EV_KEY)
    {
        switch(event.value)
        {
            case 0:
                if(s_key_task_t.last_key_event == KEY_EVENT_BTN_PRESS)
                    s_key_task_t.key_event = KEY_EVENT_BTN_PRESS_UP;
                else if(s_key_task_t.last_key_event == KEY_EVENT_BTN_HOLD)
                    s_key_task_t.key_event = KEY_EVENT_BTN_HOLD_UP;
                else
                    s_key_task_t.key_event = KEY_EVENT_UNKNOW;
                break;
            case 1:
                last_press_time = recv_time_ms;
                s_key_task_t.key_event = KEY_EVENT_BTN_PRESS;
                break;
            case 2:
                if((s_key_task_t.last_key_event == KEY_EVENT_BTN_PRESS || s_key_task_t.last_key_event == KEY_EVENT_BTN_HOLD) && (recv_time_ms-last_press_time > 1000))
                    s_key_task_t.key_event = KEY_EVENT_BTN_HOLD;
                else
                    s_key_task_t.key_event = KEY_EVENT_BTN_PRESS;
                break;
            default:
                s_key_task_t.key_event = KEY_EVENT_UNKNOW;
                break;
        }
    }
    else
    {
        return false;
    }
    s_key_task_t.last_key_event = s_key_task_t.key_event;
    return true;
}

int main()
{
    bool ret;
    struct mq_attr attr;
    memset(&attr, 0x00, sizeof(attr));
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = MSG_SIZE;
    attr.mq_curmsgs = 0;

    s_key_task_t.qfd = mq_open(QUEUE_NAME, O_RDWR | O_CREAT, 0644, &attr);
    if(s_key_task_t.qfd == (mqd_t)-1)
    {
        syslog(LOG_ERR,"error: can't not open the queue %s because %s\n", QUEUE_NAME, strerror(errno));
        return -1;
    }

    s_key_task_t.key_encoder_fd = open(ENCODER_KEY_EVENT, O_RDONLY);
    if(s_key_task_t.key_encoder_fd < 0)
    {
        syslog(LOG_ERR,"error: can't not open the file %s because %s\n", ENCODER_KEY_EVENT, strerror(errno));
        return -1;
    }

    s_key_task_t.key_gpio_fd = open(GPIO_KEY_EVENT, O_RDONLY);
    if(s_key_task_t.key_gpio_fd < 0)
    {
        syslog(LOG_ERR,"error: can't not open the file %s because %s\n", GPIO_KEY_EVENT, strerror(errno));
        close(s_key_task_t.key_encoder_fd);
        return -1;
    }

    pthread_create(&s_key_task_t.key_recv_thread, NULL, key_thread, NULL);
    pthread_detach(s_key_task_t.key_recv_thread);
    while(1)
    {
        ret = get_key_event();
        if(ret == true)
        {
            switch(s_key_task_t.key_event)
            {
                case KEY_EVENT_ENCODER_FRONT:
                    syslog(LOG_DEBUG,"KEY_EVENT_ENCODER_FRONT\n");
                    break;
                case KEY_EVENT_ENCODER_BACK:
                    syslog(LOG_DEBUG,"KEY_EVENT_ENCODER_BACK\n");
                    break;
                case KEY_EVENT_BTN_PRESS_UP:
                    syslog(LOG_DEBUG,"KEY_EVENT_BTN_PRESS_UP\n");
                    break;
                case KEY_EVENT_BTN_HOLD:
                    syslog(LOG_DEBUG,"KEY_EVENT_BTN_HOLD\n");
                    if(is_red_led_on)
                        system(RED_LED_DEFAULT_ON_CMD);
                    else
                        system(GREEN_LED_DEFAULT_ON_CMD);
                    break;
                case KEY_EVENT_BTN_HOLD_UP:
                    syslog(LOG_DEBUG,"KEY_EVENT_BTN_HOLD_UP\n");

                    ret = check_xfce_running();
                    if(ret){
                        system(GREEN_LED_OFF_CMD);
                        system(RED_LED_HEARTBEAT_CMD);
                        system(EXIT_XFCE_DESKTOP_CMD);
                        is_red_led_on=true;
                    }else{
                        system(GREEN_LED_HEARTBEAT_CMD);
                        system(RED_LED_OFF_CMD);
                        system(ENTER_XFCE_DESKTOP_CMD);
                        is_red_led_on=false;
                    }
                    break;
                default:
                    break;
            }
        }
    }
    mq_close(s_key_task_t.qfd);
    mq_unlink(QUEUE_NAME);
    close(s_key_task_t.key_encoder_fd);
    close(s_key_task_t.key_gpio_fd);
    return 0;
}
