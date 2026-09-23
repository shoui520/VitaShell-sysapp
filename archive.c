/*
  VitaShell
  Copyright (C) 2015-2018, TheFloW

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "main.h"
#include "browser.h"
#include "archive.h"
#include "psarc.h"
#include "file.h"
#include "utils.h"
#include "elf.h"

static int is_psarc = 0;
static char archive_file[MAX_PATH_LENGTH];
static int archive_path_start = 0;
struct archive *archive_fd = NULL;
static int need_password = 0;
static char password[128];

int checkForUnsafeImports(const Elf32_Ehdr *ehdr, const Elf32_Phdr *phdr, void *buffer);
char *uncompressBuffer(const Elf32_Ehdr *ehdr, const Elf32_Phdr *phdr, const segment_info *segment,
                       const char *buffer);

void waitpid() {}
void __archive_create_child() {}
void __archive_check_child() {}

struct archive_data {
  char *filename;
  SceUID fd;
  void *buffer;
  int block_size;
};

static const char *file_passphrase(struct archive *a, void *client_data) {
  return password;
}

static int file_open(struct archive *a, void *client_data) {
  struct archive_data *archive_data = client_data;
  
  archive_data->fd = sceIoOpen(archive_data->filename, SCE_O_RDONLY, 0);
  if (archive_data->fd < 0)
    return ARCHIVE_FATAL;
  
  archive_data->buffer = memalign(4096, TRANSFER_SIZE);
  if (!archive_data->buffer) {
    sceIoClose(archive_data->fd);
    archive_data->fd = -1;
    return ARCHIVE_FATAL;
  }
  archive_data->block_size = TRANSFER_SIZE;
  
  return ARCHIVE_OK;
}

static ssize_t file_read(struct archive *a, void *client_data, const void **buff) {
  struct archive_data *archive_data = client_data;
  *buff = archive_data->buffer;
  return sceIoRead(archive_data->fd, archive_data->buffer, archive_data->block_size);
}

static int64_t file_skip(struct archive *a, void *client_data, int64_t request) {
  struct archive_data *archive_data = client_data;
  int64_t old_offset, new_offset;

  if ((old_offset = sceIoLseek(archive_data->fd, 0, SCE_SEEK_CUR)) >= 0 &&
      (new_offset = sceIoLseek(archive_data->fd, request, SCE_SEEK_CUR)) >= 0)
    return new_offset - old_offset;

  return -1;
}

static int64_t file_seek(struct archive *a, void *client_data, int64_t request, int whence) {
  struct archive_data *archive_data = client_data;
  int64_t r;

  r = sceIoLseek(archive_data->fd, request, whence);
  if (r >= 0)
    return r;

  return ARCHIVE_FATAL;
}

static int file_close2(struct archive *a, void *client_data) {
  struct archive_data *archive_data = client_data;

  if (archive_data->fd >= 0) {
    sceIoClose(archive_data->fd);
    archive_data->fd = -1;
  }

  free(archive_data->buffer);
  archive_data->buffer = NULL;

  return ARCHIVE_OK;
}

static int file_close(struct archive *a, void *client_data) {
  struct archive_data *archive_data = client_data;
  file_close2(a, client_data);
  free(archive_data->filename);
  free(archive_data);
  return ARCHIVE_OK;
}

static int file_switch(struct archive *a, void *client_data1, void *client_data2) {
  file_close2(a, client_data1);
  return file_open(a, client_data2);
}

int append_archive(struct archive *a, const char *filename) {
  struct archive_data *archive_data = calloc(1, sizeof(struct archive_data));
  if (!archive_data) return ARCHIVE_FATAL;
  archive_data->fd = -1;
  if (archive_data) {
    archive_data->filename = malloc(strlen(filename) + 1);
    if (!archive_data->filename) { free(archive_data); return ARCHIVE_FATAL; }
    strcpy(archive_data->filename, filename);
    if (archive_read_append_callback_data(a, archive_data) != ARCHIVE_OK) {
      free(archive_data->filename);
      free(archive_data);
      return ARCHIVE_FATAL;
    }
  }
  
  return ARCHIVE_OK;
}

struct archive *open_archive(const char *filename) {
  struct archive *a = archive_read_new();
  if (!a)
    return NULL;
  
  archive_read_support_filter_all(a);
  archive_read_support_format_all(a);
  
  archive_read_set_passphrase_callback(a, NULL, file_passphrase);
  archive_read_set_open_callback(a, file_open);
  archive_read_set_read_callback(a, file_read);
  archive_read_set_skip_callback(a, file_skip);
  archive_read_set_close_callback(a, file_close);
  archive_read_set_switch_callback(a, file_switch);
  archive_read_set_seek_callback(a, file_seek);
  
  // Get path and name of filename
  int type = 0;
  
  char *p = strrchr(filename, '/');
  if (!p)
    p = strrchr(filename, ':');
  if (p) {
    char *q = strrchr(p + 1, '.');
    if (q) {
      char path[MAX_PATH_LENGTH];
      char name[MAX_NAME_LENGTH];
      char new_path[MAX_PATH_LENGTH];
      char format[24];
      int part_format = 0;
      
      strncpy(path, filename, p - filename + 1);
      path[p - filename + 1] = '\0';
      
      strncpy(name, p + 1, q - (p + 1));
      name[q - (p + 1)] = '\0';
      
      // Check for multi volume rar
      if (strcasecmp(q, ".rar") == 0) {
        // Check for .partXXXX.rar archives
        q = strrchr(name, '.');
        if (q) {
          if (strncasecmp(q + 1, "part", 4) == 0) {
            name[q - name] = '\0';
            
            for (part_format = 0; part_format < 4; part_format++) {
              strcpy(format, "%s%s.part%0Xd.rar");
              format[11] = '1' + part_format;
              snprintf(new_path, MAX_PATH_LENGTH, format, path, name, 1);
              if (checkFileExist(new_path)) {
                type = 1;
                break;
              }
            }
          }
        }
        
        // Check for .rXX archives
        if (type == 0) {
          snprintf(new_path, MAX_PATH_LENGTH, "%s%s.r00", path, name);
          if (checkFileExist(new_path)) {
            strcpy(format, "%s%s.r%02d");
            type = 2;
          }
        }
        
        if (type != 0) {
          int num = 1;
          
          if (type == 2) {
            num = 0; // this type begins with .r00
            
            // Append .rar as first archive
            if (append_archive(a, filename) != ARCHIVE_OK) {
              archive_read_free(a);
              return NULL;
            }
          }
          
          // Append other parts
          while (1) {
            snprintf(new_path, MAX_PATH_LENGTH, format, path, name, num);
            if (!checkFileExist(new_path))
              break;
            
            if (append_archive(a, new_path) != ARCHIVE_OK) {
              archive_read_free(a);
              return NULL;
            }
            
            num++;
          }
        }
      }
    }
  }
  
  // Single volume
  if (type == 0) {
    if (append_archive(a, filename) != ARCHIVE_OK) {
      archive_read_free(a);
      return NULL;
    }
  }
  
  // Open archive
  if (archive_read_open1(a)) {
    archive_read_free(a);
    return NULL;
  }
  
  return a;
}

typedef struct ArchiveFileNode {
  struct ArchiveFileNode *child;
  struct ArchiveFileNode *next;
  char *name;
  SceIoStat stat;
} ArchiveFileNode;

static ArchiveFileNode *archive_root = NULL;
static unsigned int archive_node_count;

char *serializePathName(char *name, char **p) {
  if (!p)
    return name;
  
  if (*p) {
    **p = '/'; // restore
    name = *p + 1;
  }
  
  *p = strchr(name, '/');
  if (*p)
    **p = '\0';
  
  return name;
}

ArchiveFileNode *createArchiveNode(const char *name, SceIoStat *stat) {
  if (archive_node_count >= SYSAPP_MAX_LIST_ENTRIES) return NULL;
  ArchiveFileNode *node = malloc(sizeof(ArchiveFileNode));
  if (!node)
    return NULL;
  
  memset(node, 0, sizeof(ArchiveFileNode));

  node->name = malloc(strlen(name) + 1);
  if (!node->name) {
    free(node);
    return NULL;
  }
  
  strcpy(node->name, name);
  
  node->child = NULL;
  node->next = NULL;
  
  memcpy(&node->stat, stat, sizeof(SceIoStat));
  
  archive_node_count++;
  return node;
}

ArchiveFileNode *_findArchiveNode(ArchiveFileNode *parent, char *name, ArchiveFileNode **parent_out,
                                  ArchiveFileNode **prev_out, char **name_out, char **p_out) {
  char *p = NULL;
  ArchiveFileNode *prev = NULL;

  // Remove trailing slash
  removeEndSlash(name);
  
  // Serialize path name
  name = serializePathName(name, &p);
  
  // Root
  if (name[0] == '\0')
    return parent;
  
  // Traverse
  ArchiveFileNode *curr = parent->child;
  while (curr) {
    // Found name
    if (strcasecmp(curr->name, name) == 0) {
      // Found node
      if (!p)
        return curr;
      
      // Is folder
      if (SCE_S_ISDIR(curr->stat.st_mode)) {
        // Serialize path name
        name = serializePathName(name, &p);
        
        // Get child entry of this directory
        parent = curr;
        curr = curr->child;
        continue;
      }
    }
    
    // Get next entry in this directory
    prev = curr;
    curr = curr->next;
  }
  
  // Out
  if (parent_out)
    *parent_out = parent;
  if (prev_out)
    *prev_out = prev;
  if (name_out)
    *name_out = name;
  if (p_out)
    *p_out = p;
  
  return NULL;
}

ArchiveFileNode *findArchiveNode(const char *path) {
  char name[MAX_PATH_LENGTH];
  strcpy(name, path);
  return _findArchiveNode(archive_root, name, NULL, NULL, NULL, NULL);
}

int addArchiveNodeRecursive(ArchiveFileNode *parent, char *name, SceIoStat *stat) {  
  char *p = NULL;
  ArchiveFileNode *prev = NULL;
  
  if (!parent)
    return VITASHELL_ERROR_ILLEGAL_ADDR;
  
  ArchiveFileNode *res = _findArchiveNode(parent, name, &parent, &prev, &name, &p);
  
  // Already exist
  if (res) {
    memcpy(&res->stat, stat, sizeof(SceIoStat));
    return 0;
  }
  
  // Create new node
  SceIoStat node_stat;
  memcpy(&node_stat, stat, sizeof(SceIoStat));
  node_stat.st_mode = SCE_S_IFDIR;
  node_stat.st_size = 0;
  
  ArchiveFileNode *node = createArchiveNode(name, p ? &node_stat : stat);  
  if (!node) return VITASHELL_ERROR_NO_MEMORY;
  if (!parent->child) { // First child
    parent->child = node;
  } else {              // Neighbour
    prev->next = node;
  }
  
  // Recursion
  if (p)
    return addArchiveNodeRecursive(node, p + 1, stat);
  
  return 0;
}

int addArchiveNode(const char *path, SceIoStat *stat) {
  if (!path || strlen(path) >= MAX_PATH_LENGTH) return VITASHELL_ERROR_INVALID_ARGUMENT;
  unsigned int depth = 0;
  for (const char *p = path; *p; ++p) {
    if (*p == '/' && ++depth > 64) return VITASHELL_ERROR_INVALID_ARGUMENT;
  }
  char name[MAX_PATH_LENGTH];
  strcpy(name, path);
  return addArchiveNodeRecursive(archive_root, name, stat);
}

void freeArchiveNodes(ArchiveFileNode *curr) {
  // Traverse
  while (curr) {
    // Enter directory
    if (SCE_S_ISDIR(curr->stat.st_mode)) {
      // Traverse child entry of this directory
      freeArchiveNodes(curr->child);
    }
    
    // Get next entry in this directory
    ArchiveFileNode *next = curr->next;
    free(curr->name);
    free(curr);
    archive_node_count--;
    curr = next;
  }
}

/* Check a bounded SELF image. Compressed segments have their own bound. */
static int checkSelfImage(const unsigned char *buffer, size_t size) {
  if (size < 0x88) return VITASHELL_ERROR_INVALID_ARGUMENT;
  uint64_t elf_offset, phdr_offset, info_offset, authid;
  memcpy(&elf_offset, buffer + 0x40, 8);
  memcpy(&phdr_offset, buffer + 0x48, 8);
  memcpy(&info_offset, buffer + 0x58, 8);
  memcpy(&authid, buffer + 0x80, 8);
  if (elf_offset > size || sizeof(Elf32_Ehdr) > size - elf_offset)
    return VITASHELL_ERROR_INVALID_ARGUMENT;
  Elf32_Ehdr ehdr;
  memcpy(&ehdr, buffer + elf_offset, sizeof(ehdr));
  if (!ehdr.e_phnum || phdr_offset > size || info_offset > size ||
      ehdr.e_phnum > (size - phdr_offset) / sizeof(Elf32_Phdr) ||
      ehdr.e_phnum > (size - info_offset) / sizeof(segment_info))
    return VITASHELL_ERROR_INVALID_ARGUMENT;
  /* SELF tables are naturally aligned on Vita. Reject malformed offsets. */
  if ((phdr_offset & 3) || (info_offset & 7)) return VITASHELL_ERROR_INVALID_ARGUMENT;
  const Elf32_Phdr *phdr = (const Elf32_Phdr *)(buffer + phdr_offset);
  const segment_info *info = (const segment_info *)(buffer + info_offset);
  size_t total = 0;
  for (unsigned int i = 0; i < ehdr.e_phnum; ++i) {
    if (phdr[i].p_filesz > BIG_BUFFER_SIZE - total) return VITASHELL_ERROR_NO_MEMORY;
    if (info[i].offset > size || info[i].length > size - info[i].offset)
      return VITASHELL_ERROR_INVALID_ARGUMENT;
    total += phdr[i].p_filesz;
  }
  char *segments = malloc(total ? total : 1);
  if (!segments) return VITASHELL_ERROR_NO_MEMORY;
  size_t offset = 0;
  for (unsigned int i = 0; i < ehdr.e_phnum; ++i) {
    if (!phdr[i].p_filesz) continue;
    if (info[i].compression == 1) {
      if (info[i].length != phdr[i].p_filesz) { free(segments); return VITASHELL_ERROR_INVALID_ARGUMENT; }
      memcpy(segments + offset, buffer + info[i].offset, phdr[i].p_filesz);
    } else {
      uLongf out = phdr[i].p_filesz;
      if (uncompress((Bytef *)segments + offset, &out, buffer + info[i].offset, info[i].length) != Z_OK || out != phdr[i].p_filesz) {
        free(segments); return VITASHELL_ERROR_INVALID_ARGUMENT;
      }
    }
    offset += phdr[i].p_filesz;
  }
  int unsafe = checkForUnsafeImports(&ehdr, phdr, segments);
  free(segments);
  return unsafe == 0 && authid != 0x2F00000000000002 ? 1 : unsafe;
}

int archiveCheckFilesForUnsafeFself() {
  struct archive *archive = open_archive(archive_file);
  if (!archive) return VITASHELL_ERROR_NO_MEMORY;
  int result = 0;
  for (;;) {
    struct archive_entry *entry;
    int status = archive_read_next_header(archive, &entry);
    if (status == ARCHIVE_EOF) break;
    if (status != ARCHIVE_OK) { result = VITASHELL_ERROR_INTERNAL; break; }
    uint32_t magic = 0;
    if (archive_read_data(archive, &magic, sizeof(magic)) != sizeof(magic) || magic != 0x00454353)
      continue;
    int64_t length = archive_entry_size(entry);
    if (length < 0x88 || length > BIG_BUFFER_SIZE) { result = VITASHELL_ERROR_NO_MEMORY; break; }
    unsigned char *buffer = malloc(length);
    if (!buffer) { result = VITASHELL_ERROR_NO_MEMORY; break; }
    memcpy(buffer, &magic, sizeof(magic));
    size_t offset = sizeof(magic);
    while (offset < length) {
      int n = archive_read_data(archive, buffer + offset, length - offset);
      if (n <= 0) break;
      offset += n;
    }
    result = offset == length ? checkSelfImage(buffer, length) : VITASHELL_ERROR_INTERNAL;
    free(buffer);
    if (result) break;
  }
  archive_read_free(archive);
  return result;
}

int fileListGetArchiveEntries(FileList *list, const char *path, int sort) {
  if (is_psarc) return fileListGetPsarcEntries(list, path, sort);
  if (!list) return VITASHELL_ERROR_ILLEGAL_ADDR;
  FileListEntry *entry = fileListCreateEntry(DIR_UP, 1);
  if (!entry) return VITASHELL_ERROR_NO_MEMORY;
  fileListAddEntry(list, entry, sort);
  ArchiveFileNode *curr = findArchiveNode(path + archive_path_start);
  if (curr) curr = curr->child;
  for (; curr; curr = curr->next) {
    if (list->length >= SYSAPP_MAX_LIST_ENTRIES) goto no_memory;
    entry = fileListCreateEntry(curr->name, SCE_S_ISDIR(curr->stat.st_mode));
    if (!entry) goto no_memory;
    entry->size = curr->stat.st_size;
    entry->ctime = curr->stat.st_ctime;
    entry->mtime = curr->stat.st_mtime;
    entry->atime = curr->stat.st_atime;
    if (entry->is_folder) list->folders++; else list->files++;
    fileListAddEntry(list, entry, sort);
  }
  return 0;
no_memory:
  fileListEmpty(list);
  return VITASHELL_ERROR_NO_MEMORY;
}

int getArchivePathInfo(const char *path, uint64_t *size, uint32_t *folders,
                       uint32_t *files, int (* handler)(const char *path)) {
  if (sceKernelGetThreadStackFreeSize(0) < 16 * 1024) return VITASHELL_ERROR_NO_MEMORY;
  if (is_psarc)
    return getPsarcPathInfo(path, size, folders, files, handler);
  
  SceIoStat stat;
  memset(&stat, 0, sizeof(SceIoStat));

  int res = archiveFileGetstat(path, &stat);
  if (res < 0)
    return res;
  
  if (SCE_S_ISDIR(stat.st_mode)) {
    FileList list;
    memset(&list, 0, sizeof(FileList));
    res = fileListGetArchiveEntries(&list, path, SORT_NONE);
    if (res < 0) return res;

    FileListEntry *entry = list.head->next; // Ignore ..

    int i;
    for (i = 0; i < list.length - 1; i++, entry = entry->next) {
      if (strlen(path) + strlen(entry->name) + 2 > MAX_PATH_LENGTH) { fileListEmpty(&list); return VITASHELL_ERROR_INVALID_ARGUMENT; }
      char *new_path = malloc(strlen(path) + strlen(entry->name) + 2);
      if (!new_path) { fileListEmpty(&list); return VITASHELL_ERROR_NO_MEMORY; }
      snprintf(new_path, MAX_PATH_LENGTH, "%s%s", path, entry->name);
      
      if (handler && handler(new_path)) {
        free(new_path);
        continue;
      }
      
      if (entry->is_folder) {
        int ret = getArchivePathInfo(new_path, size, folders, files, handler);
        if (ret <= 0) {
          free(new_path);
          fileListEmpty(&list);
          return ret;
        }
      } else {
        if (size)
          (*size) += entry->size;

        if (files)
          (*files)++;
      }

      free(new_path);
    }

    if (folders)
      (*folders)++;

    fileListEmpty(&list);
  } else {
    if (handler && handler(path))
      return 1;
    
    if (size)
      (*size) += stat.st_size;

    if (files)
      (*files)++;
  }

  return 1;
}

int extractArchiveFile(const char *src_path, const char *dst_path, FileProcessParam *param) {
  SceUID fdsrc = archiveFileOpen(src_path, SCE_O_RDONLY, 0);
  if (fdsrc < 0)
    return fdsrc;

  void *buf = memalign(4096, TRANSFER_SIZE);
  if (!buf) { archiveFileClose(fdsrc); return VITASHELL_ERROR_NO_MEMORY; }

  SceUID fddst = sceIoOpen(dst_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fddst < 0) {
    free(buf);
    archiveFileClose(fdsrc);
    return fddst;
  }


  while (1) {
    int read = archiveFileRead(fdsrc, buf, TRANSFER_SIZE);

    if (read < 0) {
      free(buf);

      sceIoClose(fddst);
      archiveFileClose(fdsrc);

      sceIoRemove(dst_path);

      return read;
    }

    if (read == 0)
      break;

    int written = sceIoWrite(fddst, buf, read);

    if (written < 0) {
      free(buf);

      sceIoClose(fddst);
      archiveFileClose(fdsrc);

      sceIoRemove(dst_path);

      return written;
    }

    if (param) {
      if (param->value)
        (*param->value) += read;

      if (param->SetProgress)
        param->SetProgress(param->value ? *param->value : 0, param->max);

      if (param->cancelHandler && param->cancelHandler()) {
        free(buf);

        sceIoClose(fddst);
        archiveFileClose(fdsrc);

        sceIoRemove(dst_path);

        return 0;
      }
    }
  }

  free(buf);

  sceIoClose(fddst);
  archiveFileClose(fdsrc);

  return 1;
}

int extractArchivePath(const char *src_path, const char *dst_path, FileProcessParam *param) {
  if (sceKernelGetThreadStackFreeSize(0) < 16 * 1024) return VITASHELL_ERROR_NO_MEMORY;
  if (is_psarc)
    return extractPsarcPath(src_path, dst_path, param);
  
  SceIoStat stat;
  memset(&stat, 0, sizeof(SceIoStat));

  int res = archiveFileGetstat(src_path, &stat);
  if (res < 0)
    return res;
  
  if (SCE_S_ISDIR(stat.st_mode)) {
    FileList list;
    memset(&list, 0, sizeof(FileList));
    res = fileListGetArchiveEntries(&list, src_path, SORT_NONE);
    if (res < 0) return res;

    int ret = sceIoMkdir(dst_path, 0777);
    if (ret < 0 && ret != SCE_ERROR_ERRNO_EEXIST) {
      fileListEmpty(&list);
      return ret;
    }

    if (param) {
      if (param->value)
        (*param->value) += DIRECTORY_SIZE;

      if (param->SetProgress)
        param->SetProgress(param->value ? *param->value : 0, param->max);
      
      if (param->cancelHandler && param->cancelHandler()) {
        fileListEmpty(&list);
        return 0;
      }
    }

    FileListEntry *entry = list.head->next; // Ignore ..

    int i;
    for (i = 0; i < list.length - 1; i++, entry = entry->next) {
      if (strlen(src_path) + strlen(entry->name) + 2 > MAX_PATH_LENGTH) { fileListEmpty(&list); return VITASHELL_ERROR_INVALID_ARGUMENT; }
      char *new_src_path = malloc(strlen(src_path) + strlen(entry->name) + 2);
      if (!new_src_path) { fileListEmpty(&list); return VITASHELL_ERROR_NO_MEMORY; }
      snprintf(new_src_path, MAX_PATH_LENGTH, "%s%s", src_path, entry->name);

      if (strlen(dst_path) + strlen(entry->name) + 2 > MAX_PATH_LENGTH) { free(new_src_path); fileListEmpty(&list); return VITASHELL_ERROR_INVALID_ARGUMENT; }

      char *new_dst_path = malloc(strlen(dst_path) + strlen(entry->name) + 2);

      if (!new_dst_path) { free(new_src_path); fileListEmpty(&list); return VITASHELL_ERROR_NO_MEMORY; }
      snprintf(new_dst_path, MAX_PATH_LENGTH, "%s%s", dst_path, entry->name);

      int ret = 0;
      
      if (entry->is_folder) {
        ret = extractArchivePath(new_src_path, new_dst_path, param);
      } else {
        ret = extractArchiveFile(new_src_path, new_dst_path, param);
      }
        
      free(new_dst_path);
      free(new_src_path);

      if (ret <= 0) {
        fileListEmpty(&list);
        return ret;
      }
    }

    fileListEmpty(&list);
  } else {
    return extractArchiveFile(src_path, dst_path, param);
  }

  return 1;
}

int archiveFileGetstat(const char *file, SceIoStat *stat) {
  if (is_psarc)
    return psarcFileGetstat(file, stat);
    
  ArchiveFileNode *node = findArchiveNode(file + archive_path_start);
  if (!node)
    return VITASHELL_ERROR_ILLEGAL_ADDR;
  
  if (stat)
    memcpy(stat, &node->stat, sizeof(SceIoStat));
  
  return 0;
}

int archiveFileOpen(const char *file, int flags, SceMode mode) {
  if (is_psarc)
    return psarcFileOpen(file, flags, mode);
    
  // A file is already open
  if (archive_fd)
    return -1;

  // Open archive file
  archive_fd = open_archive(archive_file);
  if (!archive_fd)
    return -1;

  // Traverse
  while (1) {
    struct archive_entry *archive_entry;
    int res = archive_read_next_header(archive_fd, &archive_entry);
    if (res == ARCHIVE_EOF)
      break;
    
    if (res != ARCHIVE_OK) {
      archive_read_free(archive_fd);
      return -1;
    }
    
    // Compare pathname
    const char *name = archive_entry_pathname(archive_entry);
    if (strcasecmp(name, file + archive_path_start) == 0) {
      return ARCHIVE_FD;
    }
  }

  archive_read_free(archive_fd);
  archive_fd = NULL;

  return -1;
}

int archiveFileRead(SceUID fd, void *data, SceSize size) {
  if (is_psarc)
    return psarcFileRead(fd, data, size);
  
  if (!archive_fd || fd != ARCHIVE_FD)
    return -1;

  return archive_read_data(archive_fd, data, size);
}

int archiveFileClose(SceUID fd) {
  if (is_psarc)
    return psarcFileClose(fd);
  
  if (!archive_fd || fd != ARCHIVE_FD)
    return -1;

  archive_read_free(archive_fd);
  archive_fd = NULL;

  return 0;
}

int ReadArchiveFile(const char *file, void *buf, int size) {
  SceUID fd = archiveFileOpen(file, SCE_O_RDONLY, 0);
  if (fd < 0) return fd;
  int result = archiveFileRead(fd, buf, size);
  archiveFileClose(fd);
  return result;
}

int ReadArchiveFileBounded(const char *file, void *buf, int size) {
  SceUID fd = archiveFileOpen(file, SCE_O_RDONLY, 0);
  if (fd < 0) return fd;
  int total = 0, result = 0;
  while (total < size) {
    result = archiveFileRead(fd, (char *)buf + total, size - total);
    if (result <= 0) break;
    total += result;
  }
  if (result >= 0 && total == size) {
    char extra;
    result = archiveFileRead(fd, &extra, 1);
    if (result > 0) result = VITASHELL_ERROR_NO_MEMORY;
  }
  archiveFileClose(fd);
  return result < 0 ? result : total;
}

int archiveNeedPassword() {
  return need_password;
}

void archiveClearPassword() {
  memset(password, 0, sizeof(password));
}

void archiveSetPassword(char *string) {
  strncpy(password, string, sizeof(password) - 1);
  password[sizeof(password) - 1] = '\0';
}

int archiveClose() {
  if (is_psarc)
    return psarcClose();
  
  freeArchiveNodes(archive_root);
  archive_root = NULL;
  return 0;
}

SceMode convert_stat_mode(mode_t mode) {
  SceMode sce_mode = 0;
  if (mode & S_IFDIR)
    sce_mode |= SCE_S_IFDIR;
  if (mode & S_IFREG)
    sce_mode |= SCE_S_IFREG;
  return sce_mode;
}

int archiveOpen(const char *file) {
  // Read magic
  uint32_t magic;
  int read = ReadFile(file, &magic, sizeof(uint32_t));
  if (read < 0)
    return read;
  
  // PSARC file
  is_psarc = 0;
  if (magic == 0x52415350) {
    is_psarc = 1;
    return psarcOpen(file);
  }
  
  // Start position of the archive path
  archive_path_start = strlen(file) + 1;
  strcpy(archive_file, file);

  // Open archive file
  struct archive *archive = open_archive(file);
  if (!archive)
    return -1;
  
  // Need password?
  need_password = 0;
  if (archive_read_has_encrypted_entries(archive) == 1)
    need_password = 1;
  
  // Create archive root
  SceIoStat root_stat;
  memset(&root_stat, 0, sizeof(SceIoStat));
  root_stat.st_mode = SCE_S_IFDIR;
  archive_root = createArchiveNode("/", &root_stat);
  if (!archive_root) { archive_read_free(archive); return VITASHELL_ERROR_NO_MEMORY; }
  
  // Traverse
  while (1) {
    struct archive_entry *archive_entry;
    int res = archive_read_next_header(archive, &archive_entry);
    if (res == ARCHIVE_EOF)
      break;
    
    if (res != ARCHIVE_OK) {
      archive_read_free(archive);
      archiveClose();
      return -1;
    }
    
    // // Need password?
    if (archive_entry_is_encrypted(archive_entry))
      need_password = 1;
      
    // Get entry information
    const char *name = archive_entry_pathname(archive_entry);
    
    // File stat
    SceIoStat stat;
    memset(&stat, 0, sizeof(SceIoStat));
    
    stat.st_mode = convert_stat_mode(archive_entry_mode(archive_entry));
    stat.st_size = archive_entry_size(archive_entry);
    
    SceDateTime time;

    sceRtcSetTime_t(&time, archive_entry_ctime(archive_entry));
    convertLocalTimeToUtc(&stat.st_ctime, &time);
    
    sceRtcSetTime_t(&time, archive_entry_mtime(archive_entry));
    convertLocalTimeToUtc(&stat.st_mtime, &time);
    
    sceRtcSetTime_t(&time, archive_entry_atime(archive_entry));
    convertLocalTimeToUtc(&stat.st_atime, &time);  
    
    // Add node
    res = addArchiveNode(name, &stat);
    if (res < 0) {
      archive_read_free(archive);
      archiveClose();
      return res;
    }
  }

  archive_read_free(archive);
  return 0;
}
