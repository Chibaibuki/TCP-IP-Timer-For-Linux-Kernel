/**
 * tp_timer: timestamping for network flow analysis
 * using kernel 3.10.104
 * authors:
 *      Chibaibuki
 *      Tcz717
 * original:
 * 		Jan Demter
 * 		Christian Dickmann
 * 		Henning Peters
 */

#include <linux/types.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>

#include "tp_timer.h"

#define TP_TIMER_SPACE 500000 // number of needed tp_timer_data structs
#define TP_TIMER_CAL 100 // use specified number of first function calls for timer calibration
#define TP_TIMER_TRIMMEDMEAN 5 // percentage of trimmed mean
#define TP_TIMER_FORMAT "%-4lu id %-2hi seq %-8u thread %-8u ts %ld.%06ld x%hi\n"

static struct tp_timer_data * tp_timer_space = NULL;
static unsigned long tp_timer_count = 0;

/**
 * sequential file interface
 * handling large proc file entries
 */

// seq_operations implementation
static void * seq_start(struct seq_file * s, loff_t * pos)
{
	loff_t * spos = kmalloc(sizeof(loff_t), GFP_KERNEL);
	if (! spos)
		return NULL;
	*spos = *pos;
	return spos;
}

static void * seq_next(struct seq_file * s, void * v, loff_t * pos)
{
	loff_t * spos = (loff_t *) v;

	if (*spos >= tp_timer_count)
		return NULL; 
	
	*pos = ++(*spos);
	return spos;
}

static void seq_stop(struct seq_file * s, void * v)
{
	kfree (v);
}

static int seq_show(struct seq_file * s, void * v)
{
	struct tp_timer_data * data;
	
	if (*((loff_t *) v) >= tp_timer_count)
		return 0; 
	
	data = tp_timer_space + *((loff_t *) v);
	seq_printf(s, TP_TIMER_FORMAT,
		data->count,
		data->id,
		data->seq,
		data->threadnr,
		data->ts.tv_sec,
		data->ts.tv_usec,
		data->timesrepeated
	);

	return 0;
}

// entry point
static struct seq_operations seq_ops = {
	.start = seq_start,
	.next  = seq_next,
	.stop  = seq_stop,
	.show  = seq_show
};

static int tp_open(struct inode * inode, struct file * file)
{
	return seq_open(file, &seq_ops);
}

static int tp_release(struct inode * inode, struct file * file)
{
	// reset allocated memory
	memset(tp_timer_space, 0, sizeof(struct tp_timer_data) * TP_TIMER_SPACE);
	tp_timer_count = 0;

	// do not forget to let seq_file handle release
	return seq_release(inode, file);
}
		
static struct file_operations file_ops = {
	.owner   = THIS_MODULE,
	.open    = tp_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = tp_release
};

/**
 * calibration
 */
static unsigned long cal_usec;
static unsigned long cal_mean = 0;
static unsigned long cal_list[TP_TIMER_CAL];
static int cal_count = 0;

static void cal_start(void)
{
	struct timeval cal_ts;
	
	if (cal_count >= TP_TIMER_CAL)
		return;
			
	do_gettimeofday(&cal_ts);
	cal_usec = cal_ts.tv_usec;
}

static void cal_sort(unsigned long a[], int m, int n)
{
	unsigned long i, j, v, x;

	if (n > m) {
		i = m-1; j = n; v = a[n];
		while (1) {
			do {
				++i;
			} while (a[i] < v);
			
			do {
				--j;
			} while (a[j] > v);
			if (i >= j)
				break;
			x = a[i]; a[i] = a[j]; a[j] = x;
		}
		x = a[i]; a[i] = a[n]; a[n] = x;
		cal_sort(a, m, j);
		cal_sort(a, i+1, n);
	}
}

static void cal_print(void)
{
	int i;
	printk("cal_list:");
	for (i=0; i<TP_TIMER_CAL; i++) {
		printk(" %ld", cal_list[i]);
	}
	printk("\n");
}

static void cal_stop(unsigned long usec)
{
	int count = 0;
	int i;
	
	if (cal_count >= TP_TIMER_CAL)
		return;
	
	cal_list[cal_count] = usec - cal_usec;
	cal_count++;

	if (cal_count == TP_TIMER_CAL) {
		cal_sort(cal_list, 0, TP_TIMER_CAL - 1);
		cal_print();

		for (i=(int)(TP_TIMER_CAL/100.0*TP_TIMER_TRIMMEDMEAN); i<(int)(TP_TIMER_CAL/100.0*(100-TP_TIMER_TRIMMEDMEAN)); i++) {
			cal_mean += cal_list[i];
			count++;
		}
		cal_mean /= count;
		printk("calibration finished. runtime (%d%% trimmed mean): %ld usec\n", TP_TIMER_TRIMMEDMEAN, cal_mean);

		cal_count++;
	}
}

/**
 * creates proc file entry
 */
void tp_timer_init(void) {
	struct proc_dir_entry * entry;
	
	// reserve some memory
	tp_timer_space = vmalloc(sizeof(struct tp_timer_data) * TP_TIMER_SPACE);
	memset(tp_timer_space, 0, sizeof(struct tp_timer_data) * TP_TIMER_SPACE);
	
	// create proc file entry via seq_file interface
	entry = create_proc_entry("tp_timer", 0444, NULL);
	if (entry) {
		entry->proc_fops = &file_ops;
		entry->size = TP_TIMER_SPACE * sizeof(struct tp_timer_data);
	}
	
	printk("tp_timer_init: %d bytes allocated\n", sizeof(struct tp_timer_data) * TP_TIMER_SPACE);
}

/**
 * data collection
 */
void tp_timer_data(const short id, unsigned char * data, unsigned char * tail)
{
    int i;
	int * intdata;
	unsigned int lastSeq = 0;
	unsigned int lastThreadnr = 0;
	int count = 0;
	
	// calibration
	cal_start();
	
	// Find first 8 Bytes of ff	
	while(1) 
	{
	    for (i = 0; i < 2; i++) {    
    		if (*(((int*)data)+i) != 0xffffffff) {
			    break;
			}
		}
		if (i == 2) {
	    	break;
	    }
		
		data++;
		if (data > tail) {
				//tp_timer(id, 0, 0, 0); // do not log malformed data
				return; // Mal formed
		    }
		}
		
		// If we got more than 8 Bytes of ff in a row, find the tail of them ...
		while(*(int*)data == 0xffffffff && *(((int*)data)+1) == 0xffffffff)
		{
		    data++;
		}
		data--; // Last step was a bad one
		    
		// Skip the 8 Bytes of ff
		data += 8;
	
		intdata = (int*)data;

		// Now count the Seq Numbers
		while (intdata <= (int*)tail - 2) 
		{
		    if (*(intdata+1) != lastSeq || *(intdata) != lastThreadnr) {
				if (count != 0) {
				    tp_timer(id, lastSeq, lastThreadnr, count);
			};
			count = 0;
	    }
		    
	    lastSeq = *(intdata+1);
	    lastThreadnr = *(intdata);
	    count++;
	    intdata += 4;
	}
		
	tp_timer(id, lastSeq, lastThreadnr, count);
}

void tp_timer_seq(const short id, struct sk_buff * skb)
{

	if (skb->nh.iph != 0 && skb->nh.iph->protocol == 17) {
		unsigned char * data = (unsigned char*)skb->h.uh + 4 * 2;
		tp_timer_data(id, data, skb->tail);
	}
	
    if (skb->nh.iph == 0 || skb->nh.iph->protocol == 6) { // TCP
			
		char * doff = (((char*)(skb->h.th)+12));
		int * flags = (int*)(doff + 1);
		unsigned char * data = (unsigned char *)((unsigned char*)skb->h.th + ((*doff) >> 4 & 15) * 4);
	
		/*if (*(doff - 9) != 22) { // dport
		    return;
		}*/
	
		if(*flags & 2) // SYN
		{
		    //tp_timer(id, 1);
		    return;
		}
		
		if(*flags & 1) // FIN
		{
		    //tp_timer(id, 2);
		    return;
		}
	
		if(*flags & 4) // RST
		{
		    //tp_timer(id, 3);
		    return;
		}
	
		if((*flags & 16) && data == skb->tail) // ACK only
		{
		    //tp_timer(id, 4);
		    return;
		}

		tp_timer_data(id, data, skb->tail);
	}
}

/**
 * timestamp code
 */
void tp_timer(const short id, const unsigned int seq, const unsigned int threadnr, const short timesrepeated)
{
	struct tp_timer_data * data;

	if (tp_timer_space == NULL) {
		printk("implicit tp_timer initialization");
		tp_timer_init(); // init should be done at startup
	}
	
	if (tp_timer_count >= TP_TIMER_SPACE)
	{
		printk("tp_timer: memory space exceeded!\n");
		return;
	}
	
	data = tp_timer_space + tp_timer_count;
	data->count = tp_timer_count;
	data->id = id;
	data->seq = seq;
	data->threadnr = threadnr;
	data->timesrepeated = timesrepeated;
	
	do_gettimeofday(&(data->ts));
	cal_stop(data->ts.tv_usec);
	data->ts.tv_usec -= cal_mean;
	
	tp_timer_count++;
}

