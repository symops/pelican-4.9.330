
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>      // Required for the GPIO functions
#include <linux/interrupt.h> // Required for the IRQ code
#include <linux/kobject.h>   // Using kobjects for the sysfs bindings
#include <linux/time.h>      // Using the clock to measure time between button presses
#include <linux/kmod.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Western Digital");
MODULE_DESCRIPTION("Kamino Linux GPIO Button LKM");
MODULE_VERSION("0.2");

#define DEBOUNCE_TIME 200
#define SHORT_PRESS 0
#define LONG_PRESS 1
#define LLONG_PRESS 2

#define MONARCH_GPIO 135
#define PELICAN_GPIO 121

/* Reset Button setting */
static unsigned int pressInterval = 30;
module_param(pressInterval, uint, S_IRUGO);
MODULE_PARM_DESC(pressInterval, " Button press interval (default=30)");

#ifdef CONFIG_YODA_ONLY
/* On Yoda: 
0-30 secs: solid
31- 35 secs: fast breathing
36:-60 secs: solid 
*/
static unsigned int rtb_mlpressInterval = 35;
module_param(rtb_mlpressInterval, uint, S_IRUGO);
MODULE_PARM_DESC(rtb_mlpressInterval, " Button middle press interval (default=35)");
#endif

static unsigned int rtb_lpressInterval = 60;
module_param(rtb_lpressInterval, uint, S_IRUGO);
MODULE_PARM_DESC(rtb_lpressInterval, " Button long press interval (default=60)");

static unsigned int gpioButton = 135;    ///< Default GPIO is 135
module_param(gpioButton, uint, S_IRUGO); ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(gpioButton, " GPIO Button number (default=135)");

/* Power Button setting */
static unsigned int pwb_pressInterval = 2;
module_param(pwb_pressInterval, uint, S_IRUGO);
MODULE_PARM_DESC(pwb_pressInterval, " Button press interval (default=2)");

static unsigned int pwr_gpioButton = 123;    /* It's for pelican */
module_param(pwr_gpioButton, uint, S_IRUGO); ///< Param desc. S_IRUGO can be read/not changed
MODULE_PARM_DESC(pwr_gpioButton, " GPIO Button number (default=123)");

static char gpioName[8] = "gpioXXX"; ///< Null terminated default string -- just in case
static int irqNumber;                ///< Used to share the IRQ number within this file
static int pwr_irqNumber;
static bool isFalling = 1; ///< Rising edge is the default IRQ property

static irq_handler_t rtbgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);
static irqreturn_t rtbgpio_thread_handler(int irq, void *dev_id);
static irq_handler_t pwrgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);
static irqreturn_t pwrgpio_thread_handler(int irq, void *dev_id);

struct rtb_obj
{
    struct kobject kobj;
    int tmp;
};
#define to_rtb_obj(x) container_of(x, struct rtb_obj, kobj)

struct rtb_attribute
{
    struct attribute attr;
    ssize_t (*show)(struct rtb_obj *rtb, struct rtb_attribute *attr, char *buf);
    ssize_t (*store)(struct rtb_obj *rtb, struct rtb_attribute *attr, const char *buf, size_t count);
};
#define to_rtb_attr(x) container_of(x, struct rtb_attribute, attr)

static ssize_t rtb_attr_show(struct kobject *kobj,
                             struct attribute *attr,
                             char *buf)
{
    struct rtb_attribute *attribute;
    struct rtb_obj *rtb;

    attribute = to_rtb_attr(attr);
    rtb = to_rtb_obj(kobj);

    if (!attribute->show)
        return -EIO;

    return attribute->show(rtb, attribute, buf);
}

static ssize_t rtb_attr_store(struct kobject *kobj,
                              struct attribute *attr,
                              const char *buf, size_t len)
{
    struct rtb_attribute *attribute;
    struct rtb_obj *rtb;

    attribute = to_rtb_attr(attr);
    rtb = to_rtb_obj(kobj);

    if (!attribute->store)
        return -EIO;

    return attribute->store(rtb, attribute, buf, len);
}

static const struct sysfs_ops rtb_sysfs_ops = {
    .show = rtb_attr_show,
    .store = rtb_attr_store,
};

static void rtb_release(struct kobject *kobj)
{
    struct rtb_obj *rtb;

    rtb = to_rtb_obj(kobj);
    kfree(rtb);
}

static ssize_t rtb_show(struct rtb_obj *rtb_obj, struct rtb_attribute *attr,
                        char *buf)
{
    return sprintf(buf, "%d\n", rtb_obj->tmp);
}

static ssize_t rtb_store(struct rtb_obj *rtb_obj, struct rtb_attribute *attr,
                         const char *buf, size_t count)
{
    sscanf(buf, "%du", &rtb_obj->tmp);
    return count;
}

static struct rtb_attribute rtb_attribute =
    __ATTR(rtb, S_IRUGO | S_IWUSR, rtb_show, rtb_store);

static struct attribute *rtb_default_attrs[] = {
    &rtb_attribute.attr,
    NULL, /* need to NULL terminate the list of attributes */
};

static struct kobj_type rtb_ktype = {
    .sysfs_ops = &rtb_sysfs_ops,
    .release = rtb_release,
    .default_attrs = rtb_default_attrs,
};

static struct kset *rtb_kset;
static struct rtb_obj *rtb_obj;

static struct rtb_obj *create_rtb_obj(const char *name)
{
    struct rtb_obj *rtb;
    int retval;

    /* allocate the memory for the whole object */
    rtb = kzalloc(sizeof(*rtb), GFP_KERNEL);
    if (!rtb)
        return NULL;

    /*
     * As we have a kset for this kobject, we need to set it before calling
     * the kobject core.
     */
    rtb->kobj.kset = rtb_kset;

    /*
     * Initialize and add the kobject to the kernel.  All the default files
     * will be created here.  As we have already specified a kset for this
     * kobject, we don't have to set a parent for the kobject, the kobject
     * will be placed beneath that kset automatically.
     */
    retval = kobject_init_and_add(&rtb->kobj, &rtb_ktype, NULL, "%s", name);
    if (retval)
    {
        kobject_put(&rtb->kobj);
        return NULL;
    }

    /*
     * We are always responsible for sending the uevent that the kobject
     * was added to the system.
     */
    kobject_uevent(&rtb->kobj, KOBJ_ADD);

    return rtb;
}

static void destroy_rtb_obj(struct rtb_obj *rtb)
{
    kobject_put(&rtb->kobj);
}

int init_reset_button(void)
{
    unsigned long IRQflags = IRQF_TRIGGER_FALLING; // The default is a falling-edge interrupt
    int result = 0;

    gpio_request(gpioButton, "sysfs");            // Set up the gpioButton
    gpio_direction_input(gpioButton);             // Set the button GPIO to be an input
    gpio_set_debounce(gpioButton, DEBOUNCE_TIME); // Debounce the button with a delay of 200ms
    gpio_export(gpioButton, false);               // Causes gpio115 to appear in /sys/class/gpio

    // Perform a quick test to see that the button is working as expected on LKM load
    printk(KERN_INFO "Reset Button: The button state is currently: %d\n", gpio_get_value(gpioButton));

    /// GPIO numbers and IRQ numbers are not the same! This function performs the mapping for us
    irqNumber = gpio_to_irq(gpioButton);
    printk(KERN_INFO "Reset Button: The button is mapped to IRQ: %d\n", irqNumber);

    // If the kernel parameter isFalling=0 is supplied
    if (!isFalling)
    {
        // Set the interrupt to be on the rising edge
        IRQflags = IRQF_TRIGGER_RISING;
    }

    //roy: using kthread to check button long press
    result = request_threaded_irq(irqNumber, (irq_handler_t)rtbgpio_irq_handler, rtbgpio_thread_handler, IRQflags, "rtb_handler", NULL);

    return result;
}

int init_power_button(void)
{
    unsigned long IRQflags = IRQF_TRIGGER_FALLING; // The default is a falling-edge interrupt
    int result = 0;

    gpio_request(pwr_gpioButton, "sysfs");            // Set up the pwr_gpioButton
    gpio_direction_input(pwr_gpioButton);             // Set the button GPIO to be an input
    gpio_set_debounce(pwr_gpioButton, DEBOUNCE_TIME); // Debounce the button with a delay of 200ms
    gpio_export(pwr_gpioButton, false);

    printk(KERN_INFO "Power Button: The button state is currently: %d\n", gpio_get_value(pwr_gpioButton));

    pwr_irqNumber = gpio_to_irq(pwr_gpioButton);
    printk(KERN_INFO "Power Button: The button is mapped to IRQ: %d\n", pwr_irqNumber);

    // If the kernel parameter isFalling=0 is supplied
    if (!isFalling)
    {
        // Set the interrupt to be on the rising edge
        IRQflags = IRQF_TRIGGER_RISING;
    }

    result = request_threaded_irq(pwr_irqNumber, (irq_handler_t)pwrgpio_irq_handler, pwrgpio_thread_handler, IRQflags, "pwr_handler", NULL);

    return result;
}
/** @brief The LKM initialization function
 *  The static keyword restricts the visibility of the function to within this C file. The __init
 *  macro means that for a built-in driver (not a LKM) the function is only used at initialization
 *  time and that it can be discarded and its memory freed up after that point. In this example this
 *  function sets up the GPIOs and the IRQ
 *  @return returns 0 if successful
 */
static int __init rtbButton_init(void)
{
    int result = 0;

    printk(KERN_INFO "Button: Init RTB LKM\n");

    //roy: checking gpioButton
    if (gpioButton != MONARCH_GPIO && gpioButton != PELICAN_GPIO)
    {
        printk(KERN_INFO "Reset Button: Invalid gpioButton number [%d]!\n", gpioButton);
        return -1;
    }

    // Create the gpio135 name according to the input value
    sprintf(gpioName, "gpio%d", gpioButton);

    //roy: checking press interval
    if (pressInterval < 5)
    {
        printk(KERN_INFO "Reset Button: Invalid Button press interval [%d]!\n", pressInterval);
        return -1;
    }

    //roy: handle kobject to send uevent
    rtb_kset = kset_create_and_add("kset_rtb", NULL, kernel_kobj);
    if (!rtb_kset)
        return -ENOMEM;

    rtb_obj = create_rtb_obj("rtb");

    result = init_reset_button();
    if (result != 0)
        return result;

    if (gpioButton == PELICAN_GPIO)
    {
        result = init_power_button();
    }

    return result;
}

static void __exit rtbButton_exit(void)
{
    free_irq(irqNumber, NULL); // Free the IRQ number, no *dev_id required in this case
    gpio_unexport(gpioButton); // Unexport the Button GPIO
    gpio_free(gpioButton);     // Free the Button GPIO

    if (gpioButton == PELICAN_GPIO)
    {
        free_irq(pwr_irqNumber, NULL); // Free the IRQ number, no *dev_id required in this case
        gpio_unexport(pwr_gpioButton); // Unexport the Button GPIO
        gpio_free(pwr_gpioButton);     // Free the Button GPIO
    }
    destroy_rtb_obj(rtb_obj);
    kset_unregister(rtb_kset);

    printk(KERN_INFO "Button: Exit RTB LKM!\n");
}

#define ONESEC_MILI 1000
static irqreturn_t rtbgpio_thread_handler(int irq, void *dev_id)
{
    unsigned long startTime = 0;
    unsigned long endTime = 0;
    const int loop_sleep_time = 50;

    int error_timeout = (rtb_lpressInterval + 3) * (ONESEC_MILI / loop_sleep_time);
    int result = SHORT_PRESS;
    char event_string[20];
    char *envp[] = {event_string, NULL};
    int long_press = (pressInterval * 1000);
    int llong_press = (rtb_lpressInterval * 1000);
    unsigned long press_time = 0;
#ifdef CONFIG_YODA_ONLY
    int switch_state_long = 0;
    int switch_state_middle = 0;
    int middle_long_press = (rtb_mlpressInterval * 1000);
#endif

    //printk(KERN_INFO "Reset Button: The button state is currently: %d\n", gpio_get_value(gpioButton));
    startTime = jiffies;
#ifdef CONFIG_YODA_ONLY
    printk(KERN_INFO "Reset Button: 0s-30s state\n");
    snprintf(event_string, 20, "RTB_EVENT=0s_30s");
    kobject_uevent_env(&rtb_obj->kobj, KOBJ_CHANGE, envp);
#endif

    while (1)
    {

        /*     There is a possibility that we miss button press-release happend
       in less than DEBOUNCE_TIME(200ms), to protect against this
       remove old 200 + 800 millisecond sleep and use loop_sleep_time miliseconds
       sleep.

       As side effect it will use more cpu time,
       but will be more accurate in detecting button press-relese.
*/
        msleep(loop_sleep_time);
        if (gpio_get_value(gpioButton) == 1)
        {
            /* judge long press or short press */
            if (press_time >= long_press)
                result = LONG_PRESS;
            else
                result = SHORT_PRESS;
            break;
        }
        endTime = jiffies;
        //printk(KERN_INFO "[%d][%d][%d]\n", startTime, endTime, gpio_get_value(gpioButton));

        press_time = endTime - startTime;
        //Convert presstime into mili seconds
        press_time = (press_time * 1000) / HZ;
#ifdef CONFIG_YODA_ONLY
        if (press_time >= long_press && !switch_state_middle)
        {
            printk(KERN_INFO "Reset Button: 31s-35s state\n");
            snprintf(event_string, 20, "RTB_EVENT=31s_35s");
            kobject_uevent_env(&rtb_obj->kobj, KOBJ_CHANGE, envp);
            switch_state_middle = 1;
        }

        if (press_time >= middle_long_press && !switch_state_long)
        {
            printk(KERN_INFO "Reset Button: 36s-60s state\n");
            snprintf(event_string, 20, "RTB_EVENT=36s_60s");
            kobject_uevent_env(&rtb_obj->kobj, KOBJ_CHANGE, envp);
            switch_state_long = 1;
        }
#endif
        if (press_time >= llong_press)
        {
            result = LLONG_PRESS;
            break;
        }

        if (error_timeout < 1)
        {
            result = -1;
            break;
        }
        error_timeout--;
    }

    pr_info("{\"msgid\":\"gpioResetButton\",\"startTime\":\"%lu\",\"endTime\":\"%lu\",\"result\":\"%2d\"}", startTime, endTime, result);

    if (result == SHORT_PRESS)
    {
        printk(KERN_INFO "Reset Button: short press\n");
        snprintf(event_string, 20, "RTB_EVENT=short");
        kobject_uevent_env(&rtb_obj->kobj, KOBJ_CHANGE, envp);
    }
    else if (result == LONG_PRESS)
    {
        printk(KERN_INFO "Reset Button: long press\n");
        snprintf(event_string, 20, "RTB_EVENT=long");
        kobject_uevent_env(&rtb_obj->kobj, KOBJ_CHANGE, envp);
    }
    else if (result == LLONG_PRESS)
    {
        printk(KERN_INFO "Reset Button: llong press\n");
        snprintf(event_string, 20, "RTB_EVENT=llong");
        kobject_uevent_env(&rtb_obj->kobj, KOBJ_CHANGE, envp);
    }

    return IRQ_HANDLED;
}

static irqreturn_t pwrgpio_thread_handler(int irq, void *dev_id)
{
    int startTime = 0;
    int endTime = 0;
    int error_timeout = (pwb_pressInterval + 3);
    int result = SHORT_PRESS;
    char event_string[20];
    char *envp[] = {event_string, NULL};
    int interval = (pwb_pressInterval * 1000);

    //printk(KERN_INFO "Reset Button: The button state is currently: %d\n", gpio_get_value(gpioButton));
    startTime = (jiffies * 1000) / HZ;

    //roy: one loop is almost 1 second
    while (1)
    {
        msleep(200);
        if (gpio_get_value(pwr_gpioButton) == 1)
        {
            result = SHORT_PRESS;
            break;
        }

        msleep(800);
        endTime = (jiffies * 1000) / HZ;
        //printk(KERN_INFO "[%d][%d][%d]\n", startTime, endTime, gpio_get_value(gpioButton));

        if ((endTime - startTime) > interval)
        {
            result = LONG_PRESS;
            break;
        }

        if (error_timeout < 1)
        {
            result = -1;
            break;
        }
        error_timeout--;
    }

    if (result == SHORT_PRESS)
    {
        printk(KERN_INFO "Power Button: short press\n");
        snprintf(event_string, 20, "PWB_EVENT=short");
        kobject_uevent_env(&rtb_obj->kobj, KOBJ_CHANGE, envp);
    }

    return IRQ_HANDLED;
}

static irq_handler_t rtbgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs)
{
    return (irq_handler_t)IRQ_WAKE_THREAD;
}

static irq_handler_t pwrgpio_irq_handler(unsigned int irq, void *dev_id, struct pt_regs *regs)
{
    return (irq_handler_t)IRQ_WAKE_THREAD;
}

module_init(rtbButton_init);
module_exit(rtbButton_exit);
