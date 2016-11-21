#include <linux/fs.h>
#include <linux/vfs.h>
#include <asm/uaccess.h>
#include <linux/pagemap.h>
#include <linux/dcache.h>
#include <linux/user_namespace.h>

static struct super_operations kingfs_s_ops = {
    .statfs = simple_statfs,
    .drop_inode = generic_delete_inode,
};

static atomic_t counter;

#define TMPSIZE 20
#define KINGFS_MAGIC 0x20161101

static ssize_t kingfs_read_file(struct file *filp, char *buf,
                            size_t count, loff_t *offset)
{
    int v, len;
    char tmp[TMPSIZE];
    atomic_t *counter = (atomic_t *) filp->private_data;
    v = atomic_read(counter);
    atomic_inc(counter);

    len = snprintf(tmp, TMPSIZE, "%d\n", v);
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

static struct file_operations kingfs_file_ops = {
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
    inode->i_fop = &kingfs_file_ops;
    inode->i_private = counter;

    d_add(dentry, inode);
    return dentry;
}

static void kingfs_create_files(struct super_block *sb,
                                struct dentry *root)
{
    atomic_set(&counter, 0);
    kingfs_create_file(sb, root, "counter", &counter);
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
    inode->i_op = &simple_dir_inode_operations;
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
    return mount_single(fst, flags, data, kingfs_fill_super);
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
module_init(kingfs_init);
