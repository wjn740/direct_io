#include <linux/fs.h>
#include <linux/vfs.h>
#include <asm/uaccess.h>
#include <linux/pagemap.h>
#include <linux/dcache.h>
#include <linux/user_namespace.h>

struct inode * kingfs_make_common_inode(struct super_block *sb, const struct inode *dir, umode_t mode);

static struct super_operations kingfs_s_ops = {
    .statfs = simple_statfs,
    .drop_inode = generic_delete_inode,
};

static atomic_t counter;

#define TMPSIZE 20
#define KINGFS_MAGIC 0x20161101

static const struct address_space_operations kingfs_aops = {
	.readpage = simple_readpage,
	.write_begin = simple_write_begin,
	.write_end = simple_write_end,
};

const struct file_operations kingfs_common_file_ops = {
    .read_iter  = generic_file_read_iter,
    .write_iter = generic_file_write_iter,
    .mmap       = generic_file_mmap,
    .fsync      = noop_fsync,
    .splice_read    = generic_file_splice_read,
    .splice_write   = iter_file_splice_write,
    .llseek     = generic_file_llseek,
};

const struct inode_operations kingfs_common_file_inode_ops = {
    .setattr    = simple_setattr,
    .getattr    = simple_getattr,
};

static int kingfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode;
	inode = new_inode(dir->i_sb);
	inode_init_owner(inode, dir, mode);
	d_instantiate(dentry, inode);
	return 0;
}
static int kingfs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
			bool excl)
{
	struct inode *inode;
	inode = kingfs_make_common_inode(dir->i_sb, dir, mode);
	inode_init_owner(inode, dir, mode);
	d_instantiate(dentry, inode);
	dget(dentry);
	dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	return 0;
}
static int kingfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	inode->i_ctime = dir->i_ctime;
	mark_inode_dirty(inode);
	inode_dec_link_count(inode);
	return 0;
}
static struct inode_operations kingfs_dir_inode_operations = {
	.create = kingfs_create,
	.mkdir = kingfs_mkdir,
	.unlink = kingfs_unlink,
	.lookup = simple_lookup,
};
#if 1
static ssize_t kingfs_read_file(struct file *filp, char *buf,
                            size_t count, loff_t *offset)
{
    int v, len;
    char tmp[TMPSIZE];
    atomic_t *counter = (atomic_t *) filp->private_data;
    v = atomic_read(counter);
    if (*offset > 0) {
        ;
    }else {
        atomic_inc(counter);
    }
    len = snprintf(tmp, TMPSIZE, "%d\n", v);
    printk(KERN_INFO"%d\n",atomic_read(counter));
    if (*offset > len) {
        return 0;
    }
    if (count > len - *offset) {
        count = len - *offset;
    }
    if (copy_to_user(buf, tmp + *offset, count))
        return -EFAULT;
    *offset += count;
    return count;
}
#endif

static ssize_t kingfs_write_file(struct file *filp, const char *buf,
                            size_t count, loff_t *offset)
{
    char tmp[TMPSIZE];
    atomic_t *counter = (atomic_t *)filp->private_data;
    if (*offset != 0) {
        return -EINVAL;
    }
    if (count >= TMPSIZE) {
        return -EINVAL;
    }
    memset(tmp, 0, TMPSIZE);
    if (copy_from_user(tmp, buf, count))
        return -EFAULT;
    atomic_set(counter, simple_strtol(tmp, NULL, 10));
    return count;
}

static int kingfs_open(struct inode *inode, struct file *filp)
{
    filp->private_data = inode->i_private;
    return 0;
}


static struct file_operations kingfs_counter_file_ops = {
    .open = kingfs_open,
    .read = kingfs_read_file,
    .write = kingfs_write_file,
};



static struct inode *kingfs_make_inode(struct super_block *sb, int mode)
{
    struct inode *ret = new_inode(sb);

    if(ret) {
        ret->i_mode = mode;
        ret->i_uid = make_kuid(current_user_ns(), 0); 
        ret->i_gid = make_kgid(current_user_ns(), 0); 
        ret->i_blkbits= PAGE_CACHE_SHIFT;
        ret->i_blocks = 0;
        ret->i_atime = ret->i_mtime = ret->i_ctime = CURRENT_TIME;
    }
    return ret;
}
static struct dentry * kingfs_create_file(struct super_block *sb,
                                        struct dentry *dir, const char *name,
                                        atomic_t *counter)
{
    struct dentry *dentry;
    struct inode *inode;
    struct qstr qname;
    
    qname.name = name;
    qname.len = strlen(name);
    qname.hash = full_name_hash(name, qname.len);
    dentry = d_alloc(dir, &qname);

    inode = kingfs_make_inode(sb, S_IFREG|0644);
    if (!inode) {
        return NULL;
    }
    inode->i_fop = &kingfs_counter_file_ops;
    inode->i_private = counter;

    d_add(dentry, inode);
    printk(KERN_INFO"kingfs: create counter.\n");
    return dentry;
}

static void kingfs_create_files(struct super_block *sb,
                                struct dentry *root)
{
    atomic_set(&counter, 0);
    kingfs_create_file(sb, root, "counter", &counter);
}

struct inode * kingfs_make_common_inode(struct super_block *sb,
			const struct inode *dir, umode_t mode)
{
	struct inode *inode = new_inode(sb);
	if (inode) {
		inode_init_owner(inode, dir, mode);
		inode->i_mapping->a_ops = &kingfs_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		case S_IFDIR:
		inode->i_fop = &simple_dir_operations;
		inode->i_op = &kingfs_dir_inode_operations;
		break;
		case S_IFREG:
		inode->i_fop = &kingfs_common_file_ops;
		inode->i_op = &kingfs_common_file_inode_ops;
		break;
		}
	}
	return inode;
}

static int kingfs_fill_super(struct super_block *sb,
                            void *data, int slient)
{
    struct inode *inode;
    struct dentry *root_dentry;

    sb->s_blocksize = PAGE_CACHE_SIZE;
    sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
    sb->s_magic = KINGFS_MAGIC;
    sb->s_op = &kingfs_s_ops;

    inode = kingfs_make_inode(sb, S_IFDIR|0755);
    if (!inode) {
        return -EFAULT;
    }
    inode->i_op = &kingfs_dir_inode_operations;
    //inode->i_op = &simple_dir_inode_operations;
    inode->i_fop = &simple_dir_operations;

    root_dentry = d_make_root(inode);
    if (!root_dentry) {
        return -EFAULT;
    }
    sb->s_root = root_dentry;

    kingfs_create_files(sb, root_dentry);

    return 0;
}

static struct dentry *kingfs_get_super(struct file_system_type *fst,
                    int flags, const char *devname, void *data)
{
    return mount_nodev(fst, flags, data, kingfs_fill_super);
}

static struct file_system_type kingfs_type = {
    .owner  = THIS_MODULE,
    .name   = "kingfs",
    .mount = kingfs_get_super,
    .kill_sb    = kill_litter_super,
};

static __init int kingfs_init(void)
{
    return register_filesystem(&kingfs_type);
}
static __exit void kingfs_exit(void)
{
    unregister_filesystem(&kingfs_type);
}
module_init(kingfs_init);
module_exit(kingfs_exit);
