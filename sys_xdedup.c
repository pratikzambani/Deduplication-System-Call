#include <linux/linkage.h>
#include <linux/moduleloader.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/module.h>
#include <linux/namei.h>
#include "xdedup_header.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pratik Zambani, [pzambani@cs.stonybrook.edu]");
MODULE_DESCRIPTION("Xdedup Module");

// typedef struct xdedup_params
// {
// 	char *infile1;
// 	char *infile2;
// 	char *outfile;
// 	u_int flags;
// } params;

asmlinkage extern long (*sysptr)(void *arg);

/** 
File open function - takes path, flags, rights and file pointer as arg,
returns 0 on success, errno on error
**/

void print_debug(u_int flags, char *msg)
{
	if(flags & FLAG_D)
		printk(msg);
}

void free_buffers(unsigned char **buf1, unsigned char **buf2)
{
	if(*buf1)
		kfree(*buf1);
	if(*buf2)
		kfree(*buf2);
}

int file_open(const char *path, int flags, int rights, struct file **ofilep) 
{
    struct file *filp = NULL;
    mm_segment_t oldfs;
    //int err = 0;

    oldfs = get_fs();
    set_fs(get_ds());
    filp = filp_open(path, flags, rights);
    set_fs(oldfs);
    if (IS_ERR(filp)) {
    	*ofilep = NULL;
        return PTR_ERR(filp);
    }
    *ofilep = filp;
    return 0;
}

/**
Closes an open file - takes file pointer as input, returns void
**/

void file_close(struct file **cfilep)
{
	if (*cfilep)
		filp_close(*cfilep, NULL);
	*cfilep = NULL;
}

void close_files(struct file **cfilep1, struct file **cfilep2, struct file **cfilep3, struct file **cfilep4)
{
	printk("closing all files...\n");
	file_close(cfilep1);
	file_close(cfilep2);
	file_close(cfilep3);
	file_close(cfilep4);
}

int file_read(struct file *file, unsigned long long offset, unsigned char *data, unsigned int size) 
{
    mm_segment_t oldfs;
    int ret;

    oldfs = get_fs();
    set_fs(get_ds());

    ret = vfs_read(file, data, size, &offset);

    set_fs(oldfs);
    return ret;
}

int file_write(struct file *file, unsigned long long offset, unsigned char *data, unsigned int size) 
{
    mm_segment_t oldfs;
    int ret;

    oldfs = get_fs();
    set_fs(get_ds());

    ret = vfs_write(file, data, size, &offset);

    set_fs(oldfs);
    return ret;
}

asmlinkage long xdedup(void *arg)
{
	struct xdedup_params *uparam = NULL;
	struct xdedup_params kparam;

	long cu=0;
	int i=0;
	int aok = 0;
	int ret=0;
	int retf1=0, retf2=0;
	int not_same=0, no_of_same_bytes=0, no_of_bytes_written=0;

	char *tmpfile_name = NULL;

	struct file *filp1 = NULL;
	struct file *filp2 = NULL;
	struct file *ofilp = NULL;
	struct file *otempfilp = NULL;

	umode_t infile1_rights = 0;
	umode_t ofile_rights = 0;
	umode_t tmpfile_rights = 0;

	unsigned char *data_buffer1 = NULL;
	unsigned char *data_buffer2 = NULL;

	int buffer_cnt = 0;

	struct dentry *new_dentry = NULL;
	//struct dentry *f1parent = NULL;
	struct dentry *f2parent = NULL;
	struct dentry *trap = NULL;

	printk("inside kernel xdedup received arg %p\n", arg);
	if (arg == NULL)
	{
		printk("Invalid argument received - It is null\n");
		return -EINVAL;
	}

	aok = access_ok(VERIFY_READ, arg, sizeof(arg));
	if (!aok)
	{
		printk("User arguments not accessible\n");
		return -EIO;
	}

	uparam = (struct xdedup_params *)arg;

	// User arguments validation
	if(!uparam->infile1 || !uparam->infile2)
	{
		printk("Invalid argument received - input file is null\n");
		return -EINVAL;
	}

	if(!(uparam->flags >= 0 && uparam->flags <= 7))
	{
		printk("Invalid argument received - flags not appropriate\n");
		return -EINVAL;
	}

	print_debug(uparam->flags, KERN_INFO "Verified arguments are not null and are accessible\n");

	cu = copy_from_user((void *) &kparam, (void *)arg, sizeof(kparam));
	if(cu)
	{
		printk(KERN_CRIT "User arguments not copied to kernel\n");
		return -ENOMEM;
	}

	print_debug(kparam.flags, KERN_INFO "Copied user arguments into kernel data structure\n");

	if ((kparam.flags & FLAG_N) && (kparam.flags & FLAG_P))
	{
		if(kparam.outfile)
		{
			printk(KERN_CRIT "Invalid arg - outfile should NOT be provided with -n option\n");
			return -EINVAL;
		}
	}
	else if(kparam.flags & FLAG_N)
	{
		if(kparam.outfile)
		{
			printk(KERN_CRIT "Invalid arg - outfile should NOT be provided with -n option\n");
			return -EINVAL;
		}
	}
	else if(kparam.flags & FLAG_P)
	{
		if(!kparam.outfile)
		{
			printk(KERN_CRIT "Invalid arg - outfile should BE provided with -p option\n");
			return -EINVAL;
		}
	}
	else if (!(kparam.flags & FLAG_P))
	{
		if(kparam.outfile)
		{
			printk(KERN_CRIT "Invalid arg - outfile should NOT be provided\n");
			return -EINVAL;
		}
	}

	print_debug(kparam.flags, KERN_INFO "Verified input flags and files\n");

	printk("i1_file = %s i2_file = %s outfile = %s flags = %u\n", kparam.infile1, kparam.infile2, kparam.outfile, kparam.flags);

	retf1 = file_open(kparam.infile1, O_RDONLY, 0, &filp1);
	if (retf1 != 0)
	{
		printk(KERN_CRIT "Unable to open %s\n", kparam.infile1);
		close_files(&filp1, &filp2, &ofilp, &otempfilp);
		return retf1;
	}
	infile1_rights = filp1->f_path.dentry->d_inode->i_mode;// & 0777;

	retf2 = file_open(kparam.infile2, O_RDONLY, 0, &filp2);
	if (retf2 != 0)
	{
		printk(KERN_CRIT "Unable to open %s\n", kparam.infile2);
		close_files(&filp1, &filp2, &ofilp, &otempfilp);
		return retf2;
	}

	// Check for file sizes and ownership before dedup
	// Check not valid for some combination of n and p flags

	if((!(kparam.flags & FLAG_P) && !(kparam.flags & FLAG_N)) || ((kparam.flags & FLAG_N) && !(kparam.flags & FLAG_P)))
	{
		if((filp1->f_path.dentry->d_inode->i_size != filp2->f_path.dentry->d_inode->i_size) || 
			(filp1->f_path.dentry->d_inode->i_uid.val != filp2->f_path.dentry->d_inode->i_uid.val))
		{
			print_debug(kparam.flags, KERN_INFO "File sizes/ownership is different\n");
			//printk("sizes - %d %d\n", filp1->f_path.dentry->d_inode->i_size, filp2->f_path.dentry->d_inode->i_size);
			close_files(&filp1, &filp2, &ofilp, &otempfilp);
			return -EINVAL;
		}
		print_debug(kparam.flags, KERN_INFO "Checked input files size and ownership\n");
	}

	if(!(kparam.flags & FLAG_P) && !(kparam.flags & FLAG_N))
	{
		if((filp1->f_path.dentry->d_inode->i_ino == filp2->f_path.dentry->d_inode->i_ino) && 
			(filp1->f_path.dentry->d_sb->s_uuid == filp2->f_path.dentry->d_sb->s_uuid))
		{
			print_debug(kparam.flags, KERN_CRIT "Invalid input - same files\n");
			close_files(&filp1, &filp2, &ofilp, &otempfilp);
			return -EINVAL;
		}
	}

	// Creates/opens output file in case -p flag
	if((kparam.flags & FLAG_P) && (!(kparam.flags & FLAG_N)))
	{
		// TODO - mode
		ret = file_open(kparam.outfile, O_RDONLY, 0, &ofilp);
		if (ret != 0)
		{
			tmpfile_rights = infile1_rights;
			printk("giving infile1_rights to tmp\n");
		}
		else
		{
			ofile_rights = ofilp->f_path.dentry->d_inode->i_mode;// & 0777;
			tmpfile_rights = ofile_rights;
		}
		file_close(&ofilp);

		tmpfile_name = "/tmp/xyz_abc_123_789.txt";
		ret = file_open(tmpfile_name, O_WRONLY|O_CREAT|O_TRUNC, tmpfile_rights, &otempfilp);
		if(ret != 0)
		{
			printk(KERN_CRIT "Unable to create/open %s\n", tmpfile_name);
			close_files(&filp1, &filp2, &ofilp, &otempfilp);
			return ret;
		}
	}

	// Allocating memory for 2 buffers for simultaneously reading from 2 input files
	data_buffer1 = kmalloc(sizeof(char)*BUFFER_SIZE, GFP_KERNEL);
	if (!data_buffer1)
	{
		printk("No memory for allocating buffer for file1\n");
		close_files(&filp1, &filp2, &ofilp, &otempfilp);
		return -ENOMEM;
	}

	data_buffer2 = kmalloc(sizeof(char)*BUFFER_SIZE, GFP_KERNEL);
	if (!data_buffer2)
	{
		printk("No memory for allocating buffer for file2\n");
		close_files(&filp1, &filp2, &ofilp, &otempfilp);
		kfree(data_buffer1);
		return -ENOMEM;
	}

	buffer_cnt = 0;
	not_same = 0;
	no_of_same_bytes = 0;
	no_of_bytes_written = 0;

	print_debug(kparam.flags, KERN_INFO "Starting to match the two input files...\n");
	
	while(1)
	{
		retf1 = file_read(filp1, buffer_cnt*BUFFER_SIZE, data_buffer1, BUFFER_SIZE);
		retf2 = file_read(filp2, buffer_cnt*BUFFER_SIZE, data_buffer2, BUFFER_SIZE);

		printk("data buffer len is %d %d\n", retf1, retf2);

		if(retf1 != retf2)
		{
			not_same=1;
			printk(KERN_NOTICE "Files are not same %s %s\n", kparam.infile1, kparam.infile2);
		}

		if(retf1 < 0 || retf2 < 0)
		{
			close_files(&filp1, &filp2, &ofilp, &otempfilp);
			free_buffers(&data_buffer1, &data_buffer2);
			printk(KERN_ERR "Error in reading from file\n");
			if (retf1 < 0)
				return retf1;
			return retf2;
		}

		for(i=0;i<BUFFER_SIZE;i++)
		{
			if(i < retf1 && i < retf2 && data_buffer1[i] != data_buffer2[i])
			{
				printk(KERN_NOTICE "Files are not same at index %d in block %d\n", i, buffer_cnt);
				printk(KERN_NOTICE "Characters at the position are %c %c\n", data_buffer1[i], data_buffer2[i]);
				not_same=1;
				break;
			}
			else if((i >= retf1) || (i >= retf2))
				break;
			no_of_same_bytes += 1;
		}

		if((kparam.flags & FLAG_P) && (!(kparam.flags & FLAG_N)))
		{
			no_of_bytes_written += file_write(otempfilp, no_of_bytes_written, data_buffer1, no_of_same_bytes-no_of_bytes_written);
			printk("no_of_bytes_written updated to %d\n", no_of_bytes_written);
		}

		if(not_same || retf1 < BUFFER_SIZE || retf2 < BUFFER_SIZE)
			break;

		buffer_cnt += 1;
	}

	if ((kparam.flags & FLAG_N) && (kparam.flags & FLAG_P))
	{
		printk(KERN_INFO "-n and -p flags specified. returning number of identical bytes in the input files\n");
		close_files(&filp1, &filp2, &ofilp, &otempfilp);
		free_buffers(&data_buffer1, &data_buffer2);
		return no_of_same_bytes;
	}
	else if(kparam.flags & FLAG_N)
	{
		ret = -1;
		if(filp1->f_path.dentry->d_inode->i_size == no_of_same_bytes && filp2->f_path.dentry->d_inode->i_size == no_of_same_bytes)
			ret = no_of_same_bytes;
		close_files(&filp1, &filp2, &ofilp, &otempfilp);
		free_buffers(&data_buffer1, &data_buffer2);
		printk(KERN_INFO "-n flag specified. returning number of identical bytes on matching files else -1\n");
		return ret;		
	}
	else if(kparam.flags & FLAG_P)
	{
		// int vfs_rename(struct inode *old_dir, struct dentry *old_dentry,
	 //       struct inode *new_dir, struct dentry *new_dentry,
	 //       struct inode **delegated_inode, unsigned int flags) 

		ret = file_open(kparam.outfile, O_WRONLY|O_CREAT|O_TRUNC, tmpfile_rights, &ofilp);
		if(ret != 0)
		{
			printk(KERN_CRIT "Unable to create/open %s\n", kparam.outfile);
			close_files(&filp1, &filp2, &ofilp, &otempfilp);
			return ret;
		}
		
		print_debug(kparam.flags, KERN_INFO "Taking lock and renaming temp file to out file\n");

		trap = lock_rename(otempfilp->f_path.dentry->d_parent, ofilp->f_path.dentry->d_parent);
		
		ret = vfs_rename(otempfilp->f_path.dentry->d_parent->d_inode, otempfilp->f_path.dentry, ofilp->f_path.dentry->d_parent->d_inode, ofilp->f_path.dentry, NULL, 0);
		
		unlock_rename(otempfilp->f_path.dentry->d_parent, ofilp->f_path.dentry->d_parent);
		
		if(ret != 0)
		{
			printk(KERN_CRIT "Unable to rename %s to %s\n", tmpfile_name, kparam.outfile);
			close_files(&filp1, &filp2, &ofilp, &otempfilp);
			return ret;
		}

		print_debug(kparam.flags, KERN_INFO "Successful, now returning...\n");

		close_files(&filp1, &filp2, &ofilp, &otempfilp);
		free_buffers(&data_buffer1, &data_buffer2);
		return no_of_same_bytes;
	}
	else
	{
		if((filp1->f_path.dentry->d_inode->i_size != no_of_same_bytes) || (filp2->f_path.dentry->d_inode->i_size != no_of_same_bytes))
		{
			close_files(&filp1, &filp2, &ofilp, &otempfilp);
			free_buffers(&data_buffer1, &data_buffer2);
			print_debug(kparam.flags, KERN_CRIT "-n and -p not applied. files not identical, returning -1\n");
			return -1;
		}
		print_debug(kparam.flags, KERN_INFO "Files are identical, proceeding to deduplication...\n");
	}
	
	f2parent = filp2->f_path.dentry->d_parent;
	
	inode_lock_nested(f2parent->d_inode, I_MUTEX_PARENT);
	//ret = vfs_unlink(filp2->f_path.dentry->d_parent->d_inode, filp2->f_path.dentry, NULL);
	ret = vfs_unlink(f2parent->d_inode, filp2->f_path.dentry, NULL);
	inode_unlock(f2parent->d_inode);
	if(ret)
	{
		printk("Error in unlinking, ret val %d\n", ret);
		close_files(&filp1, &filp2, &ofilp, &otempfilp);
		free_buffers(&data_buffer1, &data_buffer2);
		return -1;
	}

	//int vfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *new_dentry, struct inode **delegated_inode)

	print_debug(kparam.flags, KERN_INFO "Taking lock to create new dentry...\n");

	inode_lock(f2parent->d_inode);
	new_dentry = lookup_one_len(kparam.infile2, f2parent, strlen(kparam.infile2));
	inode_unlock(f2parent->d_inode);

	print_debug(kparam.flags, KERN_INFO "Successful, now taking lock to create hard link...\n");
	mutex_lock_nested(&(filp1->f_path.dentry->d_parent->d_inode->i_mutex),I_MUTEX_PARENT);
	ret = vfs_link(filp1->f_path.dentry, filp1->f_path.dentry->d_parent->d_inode, new_dentry, NULL);
	mutex_unlock(&(filp1->f_path.dentry->d_parent->d_inode->i_mutex));
	if(ret)
	{
		print_debug(kparam.flags, KERN_CRIT "Error in linking...\n");
		close_files(&filp1, &filp2, &ofilp, &otempfilp);
		free_buffers(&data_buffer1, &data_buffer2);
		return -1;
	}
	print_debug(kparam.flags, KERN_INFO "Successful, now returning...\n");

	close_files(&filp1, &filp2, &ofilp, &otempfilp);
	free_buffers(&data_buffer1, &data_buffer2);
    return no_of_same_bytes;
}

static int __init init_sys_xdedup(void)
{
	printk("installed new sys_xdedup module\n");
	if (sysptr == NULL)
		sysptr = xdedup;
	return 0;
}

static void  __exit exit_sys_xdedup(void)
{
	if (sysptr != NULL)
		sysptr = NULL;
	printk("removed sys_xdedup module\n");
}
module_init(init_sys_xdedup);
module_exit(exit_sys_xdedup);
MODULE_LICENSE("GPL");
