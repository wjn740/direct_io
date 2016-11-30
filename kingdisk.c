#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <uapi/linux/hdreg.h>
#include <linux/list.h>


MODULE_LICENSE("GPL");

static int kingdisk_major = 0;
module_param(kingdisk_major, int, 0);
static int hardsect_size = 512;
module_param(hardsect_size, int, 0);
static int nsectors = 1048576; /* how big it is */ 
module_param(nsectors, int, 0);
static int ndevices = 2;
module_param(ndevices, int, 0);

enum {
    RM_SIMPLE = 0,
    RM_FULL = 1,
    RM_NOQUEUE = 2,
};

static int request_mode = RM_FULL;
module_param(request_mode, int, 0);

#define SBULL_MINORS    16
#define MINOR_SHIFT     4
#define DEVNUM(kdevnum) (MINOR(kdev_t_to_nr(kdevnum)) >> MINOR_SHIFT)

#define KERNEL_SECTOR_SIZE      512
#define KERNEL_SECTOR_SHIFT	9

#define INVALIDATE_DELAY        3000*HZ


struct kingdisk_dev{
    int size;
    u8 *data;
    short users;
    short media_change;
    spinlock_t lock;
    struct request_queue *queue;
    struct gendisk *gd;
    struct timer_list timer;
};

static struct kingdisk_dev *Devices = NULL;

#if 0
static void kingdisk_transfer(struct kingdisk_dev *dev, unsigned long sector,
                unsigned long nsect, char *buffer, int write)
{
    unsigned long offset = sector*KERNEL_SECTOR_SIZE;
    unsigned long nbytes = nsect*KERNEL_SECTOR_SIZE;
    if ((offset + nbytes) > dev->size) {
        printk(KERN_NOTICE"Beyod-end write (%ld %ld)\n", offset, nbytes);
        return;
    }
    if (write)
        memcpy(dev->data + offset, buffer, nbytes);
    else
        memcpy(buffer, dev->data + offset, nbytes);
}
#endif

static int kingdisk_open(struct block_device *bdev, fmode_t mode)
{
    struct kingdisk_dev *dev = bdev->bd_disk->private_data;
    del_timer_sync(&dev->timer);
    spin_lock(&dev->lock);
    if (!dev->users) {
        check_disk_change(bdev);
    }
    dev->users++;
    spin_unlock(&dev->lock);
    return 0;
}

static void kingdisk_release(struct gendisk *disk, fmode_t mode)
{
    struct kingdisk_dev *dev = disk->private_data;

    spin_lock(&dev->lock);
    dev->users--;
    if (! dev->users) {
        dev->timer.expires = jiffies + INVALIDATE_DELAY;
        add_timer(&dev->timer);
    }
    spin_unlock(&dev->lock);

}

int kingdisk_media_changed(struct gendisk *gd)
{
    struct kingdisk_dev *dev = gd->private_data;

    return dev->media_change;
}

int kingdisk_revalidate(struct gendisk *gd)
{
    struct kingdisk_dev *dev = gd->private_data;

    if (dev->media_change) {
        dev->media_change = 0;
	printk(KERN_WARNING"kingdisk: media will be zero by timer handler.");
        memset(dev->data, 0, dev->size);
    }
    return 0;
}

int kingdisk_ioctl(struct block_device *bdev, fmode_t mode,
                unsigned int cmd, unsigned long arg)
{
    long size;
    struct hd_geometry geo;
    struct kingdisk_dev *dev = bdev->bd_disk->private_data;
    switch(cmd) {
        case HDIO_GETGEO:
            size = dev->size*(hardsect_size/KERNEL_SECTOR_SIZE);
            geo.cylinders = (size & ~0x3f) >> 6;
            geo.heads = 4;
            geo.sectors = 16;
            geo.start = 5;
            if (copy_to_user((void __user *)arg, &geo, sizeof(geo)))
                return -EFAULT;
            return 0;
    }
    return -ENOTTY;
}

#if 0

static void kingdisk_request(struct request_queue *q)
{
    struct request *req;

    while((req = blk_fetch_request(q)) != NULL) {
        struct kingdisk_dev *dev = req->rq_disk->private_data;
        //if (! blk_fs_request(req)) {
        if (req == NULL || (req->cmd_type != REQ_TYPE_FS)) {
            printk(KERN_NOTICE"Skip non-fs request\n");
            blk_end_request_all(req, -EIO);
            continue;
        }
        kingdisk_transfer(dev, blk_rq_pos(req), blk_rq_cur_sectors(req),
                    req->buffer, rq_data_dir(req));
        blk_end_request(req, 0, blk_rq_bytes(req));
    }
}
#endif
static void kingdisk_transferv2(struct kingdisk_dev *dev, struct page *page,
                            unsigned int len, unsigned int off, int rw,
                            sector_t sector)
{
    void *buffer = kmap_atomic(page);
    size_t offset_on_disk = sector << KERNEL_SECTOR_SHIFT;

    if (rw == READ) {
        memcpy(buffer + off, dev->data + offset_on_disk, len);
        flush_dcache_page(page);
	printk("read sector %ld", sector);
    }else {
        flush_dcache_page(page);
        memcpy(dev->data + offset_on_disk, buffer + off, len);
	printk("write sector %ld", sector);
    }
    kunmap_atomic(buffer); 
}
static int kingdisk_xfer_bio(struct kingdisk_dev *dev, struct bio *bio)
{
    struct bvec_iter iter;
    struct bio_vec bvec;
    sector_t sector = bio->bi_iter.bi_sector;
    int rw = bio_data_dir(bio);
    
	//printk(KERN_EMERG "10");
    bio_for_each_segment(bvec, bio, iter) {
#if 0
        //char *buffer = __bio_kmap_atomic(bio, iter);
        char *buffer = bvec_kmap_irq(&bvec, &flags);
	printk(KERN_NOTICE"sector=%ld, sectors=%d, buffer=%p, write=%d\n", sector, bio_sectors(bio), buffer, bio_data_dir(bio)==WRITE);
        //kingdisk_transfer(dev, sector, bio_sectors(bio),
         //          buffer, bio_data_dir(bio) == WRITE);
         if (bio_data_dir(bio) == WRITE) {
            memcpy(dev->data, buffer, bvec.bv_len);
         }else {
            memcpy(buffer, dev->data, bvec.bv_len);
         }
        sector += (bio);
	printk(KERN_INFO ".");
        //__bio_kunmap_atomic(bio);
	flush_kernel_dcache_page(bvec.bv_page);
        bvec_kunmap_irq(buffer, &flags);
#endif
        kingdisk_transferv2(dev, bvec.bv_page, bvec.bv_len, bvec.bv_offset,
                rw, sector);
        sector += bvec.bv_len >> KERNEL_SECTOR_SHIFT;
    }
	//printk(KERN_EMERG "12");
    return 0;
}

static int kingdisk_xfer_request(struct kingdisk_dev *dev, struct request *req)
{
    struct bio *bio;
    int nsect = 0;

	//printk(KERN_EMERG "6");
    __rq_for_each_bio(bio, req) {
	//printk(KERN_EMERG "7");
        kingdisk_xfer_bio(dev, bio);
        nsect += bio->bi_iter.bi_size/KERNEL_SECTOR_SIZE;
	//printk(KERN_EMERG "8");
    }
	//printk(KERN_EMERG "9");
    return nsect;
}

static void kingdisk_full_request(struct request_queue *q)
{
    struct request *req;
    int sectors_xferred;
    struct kingdisk_dev *dev = q->queuedata;
	

	//printk(KERN_EMERG "1\n");
    while((req = blk_fetch_request(q)) != NULL) {
	//printk(KERN_EMERG "2");
        //if (! blk_fs_request(req)) {
        if (req == NULL || (req->cmd_type != REQ_TYPE_FS)) {
            printk(KERN_NOTICE "skip non-fs request\n");
            blk_end_request_all(req, 0);
		//printk(KERN_EMERG "3\n");
            continue;
        }
	//printk(KERN_EMERG "4");
        sectors_xferred = kingdisk_xfer_request(dev, req);

	spin_unlock_irq(q->queue_lock);
	//WARN_ON(!list_empty(&req->queuelist));
	INIT_LIST_HEAD(&req->queuelist);
	blk_end_request_all(req, 0);
	spin_lock_irq(q->queue_lock);
	//printk(KERN_EMERG "5");
    }
}



static void  kingdisk_make_request(struct request_queue *q, struct bio *bio)
{
    struct kingdisk_dev *dev = q->queuedata;

    kingdisk_xfer_bio(dev, bio);
    bio_endio(bio, 0);
}


static struct block_device_operations kingdisk_ops = {
    .owner              = THIS_MODULE,
    .open               = kingdisk_open,
    .release            = kingdisk_release,
    .media_changed      = kingdisk_media_changed,
    .revalidate_disk    = kingdisk_revalidate,
    .ioctl              = kingdisk_ioctl,
};

void kingdisk_invalidate(unsigned long ldev)
{
    struct kingdisk_dev *dev  = (struct kingdisk_dev *) ldev;
    spin_lock(&dev->lock);
    if (dev->users || !dev->data)
        printk(KERN_WARNING "kingdisk: timer snity check failed\n");
    else
        dev->media_change = 1;
    spin_unlock(&dev->lock);
}


static void setup_device(struct kingdisk_dev *dev, int which)
{
    /*
     * Get some memory.
     */
    memset(dev, 0, sizeof(struct kingdisk_dev));
    dev->size = nsectors*hardsect_size;
    dev->data = vmalloc(dev->size);
	memset(dev->data, 0, dev->size);
    if (dev->data == NULL) {
        printk(KERN_NOTICE"kingdisk drvier: allocate virtual space \
                        for kingdisk via vmalloc is failed.\n");
        return;
    }
    spin_lock_init(&dev->lock);

    init_timer(&dev->timer);
    dev->timer.data = (unsigned long) dev; 
    dev->timer.function = kingdisk_invalidate;

    switch(request_mode) {
        case RM_NOQUEUE:
            dev->queue = blk_alloc_queue(GFP_KERNEL);
            if (dev->queue == NULL) {
                goto out_vfree;
            }
            blk_queue_make_request(dev->queue, kingdisk_make_request);
            break;
        case RM_FULL:
            dev->queue = blk_init_queue(kingdisk_full_request, &dev->lock);
            if (dev->queue == NULL)
                goto out_vfree;
            break;
        default:
            printk(KERN_NOTICE "bad request mode %d, using simple\n", request_mode);
                goto out_vfree;
            break;
#if 0
        case RM_SIMPLE:
            dev->queue = blk_init_queue(kingdisk_request, &dev->lock);
            if (dev->queue == NULL)
                goto out_vfree;
            break;
#endif
    }
    blk_queue_max_hw_sectors(dev->queue, hardsect_size);
    dev->queue->queuedata = dev;

    dev->gd = alloc_disk(SBULL_MINORS);
    if (!dev->gd) {
        printk(KERN_NOTICE "alloc_disk failure\n");
        goto out_vfree;
    }
    dev->gd->major = kingdisk_major;
    dev->gd->first_minor = which*SBULL_MINORS;
    dev->gd->fops = &kingdisk_ops;
    dev->gd->queue = dev->queue;
    dev->gd->private_data = dev;
    snprintf(dev->gd->disk_name, 32, "kingdisk%c", which + 'a');
    set_capacity(dev->gd, nsectors*(hardsect_size / KERNEL_SECTOR_SIZE));
    add_disk(dev->gd);
    return;

out_vfree:
    if (dev->data)
        vfree(dev->data);
}

static struct kobject *default_probe(dev_t devt, int *partno, void *data)
{
	return NULL;
}


static __init int kingdisk_init(void)
{
    int i;

    kingdisk_major = register_blkdev(kingdisk_major, "kingdisk");
    if (kingdisk_major <= 0) {
        printk(KERN_WARNING"kingdisk driver: unable to get major number.\n");
        return -EBUSY;
    }
    blk_register_region(kingdisk_major, 16, NULL, default_probe, NULL, NULL);
    Devices = kmalloc(ndevices * sizeof(struct kingdisk_dev), GFP_KERNEL);
    if (Devices == NULL) {
        goto out_unregister;
    }
    for (i = 0; i< ndevices; i++) {
        setup_device(Devices + i, i);
    }
    printk(KERN_WARNING"kingdisk driver: disks is ready for you.\n");
    return 0;

out_unregister:
    unregister_blkdev(kingdisk_major, "kingdisk");
    return -ENOMEM;
    
}

static void kingdisk_exit(void)
{
    int i;
    for (i = 0; i < ndevices; i++) {
        struct kingdisk_dev *dev = Devices + i;

        del_timer_sync(&dev->timer);
        if (dev->gd) {
            del_gendisk(dev->gd);
            put_disk(dev->gd);
        }
        if (dev->queue) {
            if (request_mode == RM_NOQUEUE) {
                blk_put_queue(dev->queue);
            }else {
                blk_cleanup_queue(dev->queue);
            }
        }
        if (dev->data) {
            vfree(dev->data);
        }
    }
    unregister_blkdev(kingdisk_major, "kingdisk");
    kfree(Devices);
}
module_init(kingdisk_init);
module_exit(kingdisk_exit);
