// interaction with poll/select
// fixme: how connect with read/write
//
// "Целью вызовов poll и select является определить заранее,
// будет ли операция ввода/вывода блокирована."
//
// wait something
// http://stackoverflow.com/questions/12117227/linux-kernel-wait-for-other-threads-to-complete
// not work in x64
//interruptible_sleep_on( &s_q_wait );  // very old! never use it
//
// http://www.ibm.com/developerworks/ru/library/l-linux_kernel_22/

/**

 хотелось бы сделать поток который делает работу которую ему отправляем
 и сигнализирует когда она закончена
 http://www.ibm.com/developerworks/library/l-tasklets/

 http://stackoverflow.com/questions/23094444/difference-b-w-kthread-and-work-queues
 http://stackoverflow.com/questions/2147299/when-to-use-kernel-threads-vs-workqueues-in-the-linux-kernel

 http://ecee.colorado.edu/~siewerts/extra/code/example_code_archive/a102_code/EXAMPLES/Cooperstein-Drivers/s_20/lab4_all_thread.c
 tasklet? workqueue?
 http://rounder.googlecode.com/svn/trunk/ldd/hello/hello_kthread/hello_kthread.c
 это исполняется на разных ядрах?
 http://kukuruku.co/hub/nix/multitasking-in-the-linux-kernel-workqueues

 contexts:
 http://stackoverflow.com/questions/9389688/in-what-context-kernel-thread-runs-in-linux
 https://www.quora.com/What-is-a-process-context-when-talking-about-IRQ-registration

 scheduling:
 http://viewer.media.bitpipe.com/1090331926_980/1182181756_632/a3.pdf
 http://kukuruku.co/hub/opensource/multitasking-management-in-the-operating-system-kernel
 https://lwn.net/Articles/392783/

 data queue and other kernel data structs:
 http://www.slideshare.net/assinha/linux-kernel-data-structure
 http://stackoverflow.com/questions/389582/queues-in-the-linux-kernel
 */

#include "user_kern.h"

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/ioctl.h>
#include <linux/fcntl.h>
#include <linux/wait.h>  // не хватает хедеров
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/gfp.h>

#include <asm/uaccess.h>
#include <asm/atomic.h>

MODULE_LICENSE("Dual BSD/GPL");

#define DEVICE_NAME "stream_dvr0"
#define CLASS_NAME "stream0"
#define LEN_MSG 256
#define EOF 0

typedef struct
{
	struct work_struct my_work;
	int id;
} work_struct_wrapper_t;

static ssize_t write( struct file *file, const char *buf, size_t count,
        loff_t *ppos );
static unsigned int poll( struct file *file,
        struct poll_table_struct *poll_table_ptr );

static atomic_t done_ = ATOMIC_INIT(1);
static int major_n_;
static struct class* sdvr_cls_ = NULL;
static struct device* sdvr_dev_ = NULL;

// fixme: сделать открытие в неблокирующем режиме
static struct file_operations fops_ = { .write = write, .poll = poll, };

// "A wait queue is just what it sounds like: a list of processes,
// all waiting for a specific event."
//
// http://stackoverflow.com/questions/13787726/how-to-use-wake-up-interruptible
//
// call for on wait queue
// fixme: как возвращается в залоченное состояние?
// http://stackoverflow.com/questions/30234496/why-do-we-need-to-call-poll-wait-in-poll
// fixme: я не понимаю как это работает
//
// https://blackfin.uclinux.org/doku.php?id=linux-kernel:polling
//
// https://groups.yahoo.com/neo/groups/linux-bangalore-programming/conversations/topics/7527
// http://tali.admingilde.org/dhwk/vorlesung/ar01s08.html
// https://www.quora.com/How-often-does-poll_wait-return-if-there-is-data-available-to-read
//
// fixme: как происходит чтение а не запись?
// fixme: что еще заставляет его пробудится?
//- Register any wait queues used by the driver with the poll system.
//- Check for any pending events and return a mask with appropriate bits set.
//
// "after you wake up, and you must check to
// ensure that the condition you were waiting for is, indeed, true"
//
static DECLARE_WAIT_QUEUE_HEAD( q_wait_ );

//=========================================================

static void wq_func( struct work_struct *work )
{
	work_struct_wrapper_t *w = (work_struct_wrapper_t *) work;
	msleep(300);
	kfree((void *) work);

	atomic_set(&done_, 1);
	wake_up_interruptible(&q_wait_);
	return;
}

static ssize_t write( struct file *file, const char *buf, size_t count,
        loff_t *ppos )
{
	// fixme: плохая обработка ошибок
	int res = 0;
	int wrote = 0;
	struct img_hndl_t h;
	work_struct_wrapper_t *work;

	//if  // nonblocking really
	if( !atomic_read(&done_) ){
		return -EAGAIN;
	}

	wrote = sizeof(struct img_hndl_t);
	if( count != wrote ){
		return -EAGAIN;
	}

	res = copy_from_user((void*) &h, (void*) buf, count);
	if( res != 0 ){
		return -EAGAIN;
	}

	work = (work_struct_wrapper_t *) kmalloc(sizeof(work_struct_wrapper_t),
	GFP_KERNEL);
	INIT_WORK((struct work_struct * )work, wq_func);

	work->id = h.id;
	res = queue_work(system_unbound_wq, (struct work_struct *) work);
	atomic_set(&done_, 0);

	return wrote;
}

static unsigned int poll( struct file *file,
        struct poll_table_struct *poll_table_ptr )
{
	int flag = 0;
	poll_wait(file, &q_wait_, poll_table_ptr);
	if( atomic_read(&done_) ){
		flag = POLLOUT | POLLWRNORM;
	}
	return flag;
}

//=========================================================

static int stream_dvr_init( void )
{
	major_n_ = register_chrdev(0, DEVICE_NAME, &fops_);
	if( major_n_ < 0 ){
		return major_n_;
	}

	sdvr_cls_ = class_create(THIS_MODULE, CLASS_NAME);
	if( IS_ERR(sdvr_cls_) ){
		class_destroy(sdvr_cls_);
		unregister_chrdev(major_n_, DEVICE_NAME);
		return PTR_ERR(sdvr_cls_);
	}

	sdvr_dev_ = device_create(sdvr_cls_, NULL, MKDEV(major_n_, 0), NULL,
	DEVICE_NAME);
	if( IS_ERR(sdvr_dev_) ){
		class_destroy(sdvr_cls_);
		unregister_chrdev(major_n_, DEVICE_NAME);
		return PTR_ERR(sdvr_dev_);
	}

	printk( KERN_INFO "StreamDvr: loaded" );
	return 0;
}

static void stream_dvr_exit( void )
{
	device_destroy(sdvr_cls_, MKDEV(major_n_, 0));
	class_unregister(sdvr_cls_);
	class_destroy(sdvr_cls_);
	unregister_chrdev(major_n_, DEVICE_NAME);

printk(KERN_INFO "StreamDvr: unloaded\n");
}

module_init(stream_dvr_init);
module_exit(stream_dvr_exit);

