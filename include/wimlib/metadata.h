#ifndef _WIMLIB_METADATA_H
#define _WIMLIB_METADATA_H

#include "wimlib/list.h"
#include "wimlib/types.h"
#include "wimlib/wim.h"

#ifdef WITH_NTFS_3G
struct _ntfs_volume;
#endif

/* Metadata for a WIM image */
struct wim_image_metadata {

	/* Number of WIMStruct's that are sharing this image metadata (from
	 * calls to wimlib_export_image().) */
	unsigned long refcnt;

	/* Pointer to the root dentry of the image. */
	struct wim_dentry *root_dentry;

	/* Pointer to the security data of the image. */
	struct wim_security_data *security_data;

	/* Pointer to the lookup table entry for this image's metadata resource
	 */
	struct wim_lookup_table_entry *metadata_lte;

	/* Linked list of 'struct wim_inode's for this image. */
	struct list_head inode_list;

	/* Linked list of 'struct wim_lookup_table_entry's for this image that
	 * are referred to in the dentry tree, but have not had a SHA1 message
	 * digest calculated yet and therefore have not been inserted into the
	 * WIM's lookup table.  This list is added to during wimlib_add_image()
	 * and wimlib_mount_image() (read-write only). */
	struct list_head unhashed_streams;

	/* 1 iff the dentry tree has been modified.  If this is the case, the
	 * memory for the dentry tree should not be freed when switching to a
	 * different WIM image. */
	u8 modified : 1;

#ifdef WITH_NTFS_3G
	struct _ntfs_volume *ntfs_vol;
#endif
};

static inline struct wim_image_metadata *
wim_get_current_image_metadata(WIMStruct *w)
{
	return w->image_metadata[w->current_image - 1];
}

static inline const struct wim_image_metadata *
wim_get_const_current_image_metadata(const WIMStruct *w)
{
	return w->image_metadata[w->current_image - 1];
}

static inline struct wim_dentry *
wim_root_dentry(WIMStruct *w)
{
	return wim_get_current_image_metadata(w)->root_dentry;
}

static inline struct wim_security_data *
wim_security_data(WIMStruct *w)
{
	return wim_get_current_image_metadata(w)->security_data;
}

static inline const struct wim_security_data *
wim_const_security_data(const WIMStruct *w)
{
	return wim_get_const_current_image_metadata(w)->security_data;
}

/* Iterate over each inode in a WIM image that has not yet been hashed */
#define image_for_each_inode(inode, imd) \
	list_for_each_entry(inode, &imd->inode_list, i_list)

/* Iterate over each stream in a WIM image that has not yet been hashed */
#define image_for_each_unhashed_stream(lte, imd) \
	list_for_each_entry(lte, &imd->unhashed_streams, unhashed_list)

/* Iterate over each stream in a WIM image that has not yet been hashed (safe
 * against stream removal) */
#define image_for_each_unhashed_stream_safe(lte, tmp, imd) \
	list_for_each_entry_safe(lte, tmp, &imd->unhashed_streams, unhashed_list)

extern void
destroy_image_metadata(struct wim_image_metadata *imd,
		       struct wim_lookup_table *table,
		       bool free_metadata_lte);

extern void
put_image_metadata(struct wim_image_metadata *imd,
		   struct wim_lookup_table *table);

extern int
append_image_metadata(WIMStruct *w, struct wim_image_metadata *imd);

extern struct wim_image_metadata *
new_image_metadata(void);

extern struct wim_image_metadata **
new_image_metadata_array(unsigned num_images);


#endif /* _WIMLIB_METADATA_H */
