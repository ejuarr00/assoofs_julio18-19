#include <linux/module.h>	  /* Needed by all modules */
#include <linux/kernel.h>	  /* Needed for KERN_INFO  */
#include <linux/init.h>		   /* Needed for the macros */
#include <linux/fs.h>		   /* libfs stuff           */
#include <linux/uaccess.h>	 /* copy_to_user          */
#include <linux/buffer_head.h> /* buffer_head           */
#include <linux/slab.h>		   /* kmem_cache            */
#include "assoofs.h"
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eduardo Juárez Robles");

/********************************************************************************
******* declaracion de funciones y estructuras
*********************************************************************************/

int assoofs_fill_super(struct super_block *sb, void *data, int silent);
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data);
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no);
ssize_t assoofs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos);
int assoofs_sb_get_a_freeblock(struct super_block *vsb, uint64_t *block);
void assoofs_save_sb_info(struct super_block *vsb);
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
static int assoofs_create_object(struct inode *dir, struct dentry *dentry, umode_t mode);
struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search);
int assoofs_inode_save(struct super_block *sb, struct assoofs_inode_info *inode_info);
void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode);
static struct inode *assoofs_get_inode(struct super_block *sb, int ino);
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);
ssize_t assoofs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos);
static struct kmem_cache *assoofs_inode_cache;

static struct file_system_type assoofs_type = {
	.owner = THIS_MODULE,
	.name = "assoofs",
	.mount = assoofs_mount,
	.kill_sb = kill_litter_super,
};
static const struct super_operations assoofs_sops = {
	.drop_inode = generic_delete_inode,
};
static struct inode_operations assoofs_inode_ops = {
	.lookup = assoofs_lookup,
	.create = assoofs_create,
	.mkdir = assoofs_mkdir,
};
const struct file_operations assoofs_file_operations =
	{
		.read = assoofs_read,
		.write = assoofs_write,
};

const struct file_operations assoofs_dir_operations =
	{
		.owner = THIS_MODULE,
		.iterate = assoofs_iterate,
};

void assoofs_destroy_inode(struct inode *inode)
{
	struct assoofs_inode *inode_info = inode->i_private;
	printk(KERN_INFO "Freeing provate data of inode %p (%lu)\n", inode_info, inode->i_ino);
	kmem_cache_free(assoofs_inode_cache, inode_info);
}

/********************************************************************************
******* apartado 2.3.1 assoofs_init, assoofs_exit
*********************************************************************************/

static int __init assoofs_init(void)
{
	int ret;
	assoofs_inode_cache = kmem_cache_create("assoofs_inode_cache", sizeof(struct assoofs_inode_info), 0, (SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD), NULL);
	if (!assoofs_inode_cache)
	{				
		return -ENOMEM;
	}
	ret = register_filesystem(&assoofs_type);

	if (ret == 0)
		printk(KERN_INFO "Iniciando el sistema de archivos\n");
	else
		printk(KERN_ERR "No se ha podido iniciar\n");
	return ret;
}

static void __exit assoofs_exit(void)
{
	int ret;
	ret = unregister_filesystem(&assoofs_type);
	kmem_cache_destroy(assoofs_inode_cache);

	if (ret == 0)
		printk(KERN_INFO "Finalizando el sistema de archivos\n");
	else
		printk(KERN_ERR "Error al finalizar el sistema de archivo\n");
}

/********************************************************************************
******* apartado 2.3.2 assoofs_mount
*********************************************************************************/

static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
	struct dentry *ret;
	ret = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);

	if (IS_ERR(ret))
		printk(KERN_ERR "Error montando assoofs");
	else
		printk(KERN_INFO "assoofs montado exitoxamente en %s\n", dev_name);

	return ret;
}

/********************************************************************************
******* apartado 2.3.3 assoofs_fill_super
*********************************************************************************/

int assoofs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct buffer_head *b;
	struct assoofs_super_block_info *sb_info;
	struct inode *root_inode;

	printk(KERN_DEBUG "-fill_super-\n");

	// Leer info superbloque
	b = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
	sb_info = (struct assoofs_super_block_info *)b->b_data;

	// Comprobar info superbloque
	if (sb_info->magic != ASSOOFS_MAGIC)
	{
		printk(KERN_ERR "Número mágico erróneo: %llu\n", sb_info->magic);
		return -1;
	}
	else
		printk(KERN_INFO "Número mágico obtenido: %llu\n", sb_info->magic);

	if (sb_info->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE)
	{
		printk(KERN_ERR "Tamaño de bloque erróneo\n");
		return -1;
	}

	printk(KERN_INFO "Sistema de ficheros assoofs en versión %llu formateado con un tamaño de bloque %llu\n", sb_info->version, sb_info->block_size);

	//lleno el superbloque
	sb->s_magic = ASSOOFS_MAGIC;
	sb->s_fs_info = sb_info;
	sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;
	sb->s_op = &assoofs_sops;

	// Creo el inodo directorio raíz
	root_inode = new_inode(sb);

	root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER;
	root_inode->i_sb = sb;
	root_inode->i_op = &assoofs_inode_ops;
	root_inode->i_fop = &assoofs_dir_operations;
	root_inode->i_atime = current_time(root_inode);
	root_inode->i_mtime = current_time(root_inode);
	root_inode->i_ctime = current_time(root_inode);
	root_inode->i_private = assoofs_get_inode_info(sb, ASSOOFS_ROOTDIR_INODE_NUMBER);

	inode_init_owner(root_inode, NULL, S_IFDIR);

	sb->s_root = d_make_root(root_inode);

	brelse(b);
	return 0;
}
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no)
{

	struct buffer_head *bh;
	struct assoofs_inode_info *inode_info = NULL;
	int i = 0;
	struct assoofs_inode_info *buffer = NULL;
	struct assoofs_super_block_info *afs_sb = sb->s_fs_info;

	bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
	inode_info = (struct assoofs_inode_info *)bh->b_data;

	//recorrer almacen de inodos buscando inode_no
	//struct assoofs_superblock_info afs_sb=(struct assoofs_super_block_info *)sb->s_fs_info;
	for (i = 0; i < afs_sb->inodes_count; i++)
	{
		if (inode_info->inode_no == inode_no)
		{
			buffer=kmem_cache_alloc(assoofs_inode_cache, GFP_KERNEL);
			memcpy(buffer, inode_info, sizeof(*buffer));
			break;
		}
		inode_info++;
	}

	brelse(bh);
	return buffer;
}

/********************************************************************************
******* apartado 2,3.4, lookup, get_inode, create, 
******* sb_get_a_freeblock, save_sb_info, add_inode_info, 
******* save_inode_info, search_inode_info, create_object
*********************************************************************************/

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags)
{

	struct assoofs_inode_info *parent_info = parent_inode->i_private;
	struct super_block *sb = parent_inode->i_sb;
	struct assoofs_dir_record_entry *record;
	struct buffer_head *bh;

	int i;

	bh = sb_bread(sb, parent_info->data_block_number);
	//printk(KERN_INFO "lookup in: ino=%ilu, b=%ilu\n", parent_info-> inode_no, parent_info->data_block_number);

	//aparent = parent_inode->i_private;
	//sb = parent_inode->i_sb;

	//ahora crearemos nuestra estructura en forma de arbol de directorios

	record = (struct assoofs_dir_record_entry *)bh->b_data;
	for (i = 0; i < parent_info->dir_children_count; i++)
	{
		//printk(KERN_INFO "have a files: '%a'(ino=%illu)\n", record->filename, record->inode_no);
		if (!strcmp(record->filename, child_dentry->d_name.name))
		{
			struct inode *inode = assoofs_get_inode(sb, record->inode_no);

			inode_init_owner(inode, parent_inode, ((struct assoofs_inode_info *)inode->i_private)->mode);
			//en los apuntes ponia d_add(child_dentry, parent_inode);
			d_add(child_dentry, inode);

			printk(KERN_INFO "El inodo ha sido encontrado\n");
			brelse(bh);
			return NULL;
		}
		record++;
	}

	//printk(KERN_INFO "El inodo no ha sido encontrado\n", child_dentry->d_name.name);
	brelse(bh);
	return NULL;
}

static struct inode *assoofs_get_inode(struct super_block *sb, int ino)
{
	struct inode *inode;
	struct assoofs_inode_info *inode_info;

	inode_info = assoofs_get_inode_info(sb, ino);

	inode = new_inode(sb);
	inode->i_ino = ino;
	inode->i_sb = sb;
	inode->i_op = &assoofs_inode_ops;

	if (S_ISDIR(inode_info->mode))
	{
		printk(KERN_INFO "GETINODE: S_ISDIR\n");
		inode->i_fop = &assoofs_dir_operations;
	}
	else if (S_ISREG(inode_info->mode))
	{
		printk(KERN_INFO "GETINODE: S_ISREG\n");
		inode->i_fop = &assoofs_file_operations;
	}
	else
	{
		printk(KERN_ERR "Tipo de inodo desconocido, no hay directorios ni archivos\n");
	}
	inode->i_atime = current_time(inode);
	inode->i_mtime = current_time(inode);
	inode->i_ctime = current_time(inode);
	inode->i_private = inode_info;
	return inode;
}

static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	printk(KERN_INFO "Entrando en create--->\n");
	return assoofs_create_object(dir, dentry, mode);
}
static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	printk(KERN_INFO "Entrando en mkdir--->\n");
	return assoofs_create_object(dir, dentry, S_IFDIR | mode);
}
int assoofs_sb_get_a_freeblock(struct super_block *vsb, uint64_t *block)
{
	struct assoofs_super_block_info *sb = vsb->s_fs_info;
	int i;
	int ret = 0;

	/* Loop until we find a free block. We start the loop from 3,
	 * as all prior blocks will always be in use */
	for (i = ASSOOFS_RESERVED_INODES + 1; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++)
	{
		if (sb->free_blocks & (1 << i))
		{
			break;
		}
	}

	if (unlikely(i == ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED))
	{
		printk(KERN_ERR "No hay más bloques libres disponibles\n");
		ret = -ENOSPC;
	}

	*block = i;

	/* Remove the identified block from the free list */
	sb->free_blocks &= ~(1 << i);

	assoofs_save_sb_info(vsb);

	return ret;
}
void assoofs_save_sb_info(struct super_block *vsb)
{
	struct buffer_head *bh;
	struct assoofs_super_block_info *sb = vsb->s_fs_info;
	bh = sb_bread(vsb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
	bh->b_data = (char *)sb;

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}
void assoofs_add_inode_info(struct super_block *vsb, struct assoofs_inode_info *inode)
{
	struct assoofs_super_block_info *sb = vsb->s_fs_info;
	struct buffer_head *bh;
	struct assoofs_inode_info *inode_info;

	bh = sb_bread(vsb, ASSOOFS_INODESTORE_BLOCK_NUMBER);

	inode_info = (struct assoofs_inode_info *)bh->b_data;
	inode_info += sb->inodes_count;
	memcpy(inode_info, inode, sizeof(struct assoofs_inode_info));

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);

	sb->inodes_count++;
	assoofs_save_sb_info(vsb);
}

int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info)
{
	struct assoofs_inode_info *inode_iterator;
	struct buffer_head *bh;

	bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
	inode_iterator = assoofs_search_inode_info(sb, (struct assoofs_inode_info *)bh->b_data, inode_info);

	if (inode_iterator)
	{
		memcpy(inode_iterator, inode_info, sizeof(*inode_iterator));
		printk(KERN_INFO "El inodo ha sido actualizado\n");

		mark_buffer_dirty(bh);
		sync_dirty_buffer(bh);
	}
	else
	{

		printk(KERN_ERR "El nuevo tamaño de archivo no se pudo almacenar en el inodo\n");
		return -EIO;
	}

	brelse(bh);
	return 0;
}
struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search)
{
	uint64_t count = 0;
	while (start->inode_no != search->inode_no && count < ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count)
	{
		count++;
		start++;
	}

	if (start->inode_no == search->inode_no)
	{
		return start;
	}
	else
	{
		return NULL;
	}
}

static int assoofs_create_object(struct inode *dir, struct dentry *dentry, umode_t mode)
{

	struct inode *inode;
	struct assoofs_inode_info *inode_info;
	struct super_block *sb;
	struct assoofs_inode_info *parent_inode_info;
	struct buffer_head *bh;
	struct assoofs_dir_record_entry *dir_contents;
	uint64_t count;
	int ret;

	sb = dir->i_sb;
	count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count;
	inode = new_inode(sb);
	if (count < 0)
	{
		return ret;
	}
	if (unlikely(count >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED))
	{
		printk(KERN_ERR "Maximo numero de objetos soportados por assoofs\n");
		return -ENOSPC;
	}

	if (!S_ISDIR(mode) && !S_ISREG(mode))
	{
		printk(KERN_ERR "Creation request but for neither a file nor a directory");
		return -EINVAL;
	}

	if (!inode)
	{
		return -ENOMEM;
	}

	inode->i_sb = sb;
	inode->i_ino = (count + ASSOOFS_START_INO - ASSOOFS_RESERVED_INODES + 1);
	inode->i_op = &assoofs_inode_ops;
	inode->i_atime = current_time(inode);
	inode->i_mtime = current_time(inode);
	inode->i_ctime = current_time(inode);
	inode_info= kmem_cache_alloc(assoofs_inode_cache, GFP_KERNEL);
	inode_info->inode_no = inode->i_ino;
	inode_info->mode = mode;

	ret = assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);
	if (ret < 0)
	{
		printk(KERN_ERR "ASSOOFS no tiene bloques libres\n");
		return ret;
	}

	if (S_ISDIR(mode))
	{
		inode->i_fop = &assoofs_dir_operations;
		inode_info->dir_children_count = 0;
	}
	else if (S_ISREG(mode))
	{
		printk(KERN_INFO "Nueva solicitud del creacion de archivo\n");
		inode_info->file_size = 0;
		inode->i_fop = &assoofs_file_operations;
	}
	inode->i_private = inode_info;

	assoofs_add_inode_info(sb, inode_info);

	parent_inode_info = (struct assoofs_inode_info *)dir->i_private;
	bh = sb_bread(sb, parent_inode_info->data_block_number);
	dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
	dir_contents += parent_inode_info->dir_children_count;
	dir_contents->inode_no = inode_info->inode_no;

	strcpy(dir_contents->filename, dentry->d_name.name);
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	parent_inode_info->dir_children_count++;
	ret = assoofs_save_inode_info(sb, parent_inode_info);
	if (ret)
	{
		return ret;
	}
	inode_init_owner(inode, dir, mode);
	d_add(dentry, inode);
	return 0;
}

/********************************************************************************
******* iterate, read, write
*********************************************************************************/

static int assoofs_iterate(struct file *filp, struct dir_context *ctx)
{

	struct inode *inode;
	struct super_block *sb;
	struct buffer_head *bh;
	struct assoofs_inode_info *inode_info;
	struct assoofs_dir_record_entry *record;
	int i;

	//para sacar el directorio al que pertenece
	inode = filp->f_path.dentry->d_inode;
	sb = inode->i_sb;
	if (ctx->pos)
	{
		return 0;
	}
	inode_info = (struct assoofs_inode_info *)inode->i_private;
	if (!S_ISDIR(inode_info->mode))
	{
		//printk(KERN_INFO, "Entrando en iterate...\n" ,inode_info->inode_no, inode->i_ino, filp->f_path.dentry->d_name.name);
		return -1;
	}

	bh = sb_bread(sb, inode_info->data_block_number);
	record = (struct assoofs_dir_record_entry *)bh->b_data;
	for (i = 0; i < inode_info->dir_children_count; i++)
	{
		dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAXLEN, record->inode_no, DT_UNKNOWN);
		ctx->pos += sizeof(struct assoofs_dir_record_entry);
		record++;
	}
	brelse(bh);
	return 0;
}

ssize_t assoofs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
	struct assoofs_inode_info *inode_info = filp->f_path.dentry->d_inode->i_private;
	struct buffer_head *bh;
	char *buffer;
	size_t nbytes;

	if (*ppos >= inode_info->file_size)
	{
		return 0;
	}
	bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, inode_info->data_block_number);
	if (!bh)
	{
		printk(KERN_ERR "Leyendo el bloque numero %llu fallido", inode_info->data_block_number);
		return 0;
	}
	buffer = (char *)bh->b_data;
	nbytes = min((size_t)inode_info->file_size, len);
	if (copy_to_user(buf, buffer, nbytes))
	{
		brelse(bh);
		printk(KERN_ERR "Error copiando el contenido del archivo en el bufer de usuario\n");
		return -EFAULT;
	}
	*ppos += nbytes;
	brelse(bh);

	return nbytes;
}

ssize_t assoofs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
	//struct inode *inode;
	struct assoofs_inode_info *inode_info;
	struct buffer_head *bh;
	struct super_block *sb;
	char *buffer;
	int retval;

	sb = filp->f_path.dentry->d_inode->i_sb;
	//inode = filp->f_path.dentry->d_inode;
	//inode_info=inode->i_private;
	inode_info = (struct assoofs_inode_info *)filp->f_path.dentry->d_inode->i_private;
	//bh=sb_bread(sb, inode_info->data_block_number);
	bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, inode_info->data_block_number);

	if (!bh)
	{
		printk(KERN_ERR "Leyendo el bloque numero %llu fallido", inode_info->data_block_number);
		return 0;
	}
	buffer = (char *)bh->b_data;
	buffer += *ppos;
	if (copy_from_user(buffer, buf, len))
	{
		brelse(bh);
		printk(KERN_ERR "Fallo en la copia del archivo");
		//return -EFAULT;
		return -1;
	}
	*ppos += len;
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	//brelse(bh);
	inode_info->file_size = *ppos;
	retval = assoofs_save_inode_info(sb, inode_info);
	return len;
}

module_init(assoofs_init);
module_exit(assoofs_exit);
