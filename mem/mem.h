#ifndef TM_H_
#define TM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

//Self explanatory~~
void *tm_alloc(size_t size);
void tm_free(void *ptr);

#ifdef TM_IMPLEMENTATION
#define OBJECT_MAGIC            UINT64_C(0x600D0067600D6007)
#define CANARY_SIZE             sizeof(uint64_t)
#define CANARY_VALUE            UINT64_C(0x600D0067600D6007)
#define REGION_DEFAULT_CAPACITY (64 * 1024)

typedef struct Object Object;
typedef struct Region Region;

typedef struct Heap_Relm {
        Region *regions;
} Heap_Relm;

struct Region {
        Region *next;
        size_t mapping_size;
        size_t data_offset;
        size_t capacity;
        Object *free_head;
};

struct Object {
        size_t size;
        size_t requested_size;
        uint64_t magic;
        unsigned int used : 1;
        Object *prev_free;
        Object *next_free;
        Region *parent_region;
};

static Heap_Relm heap;

static size_t align_up(size_t n, size_t alignment);
static size_t object_total_size(size_t requested_size);
static void write_canary(Object *obj);
static void write_footer(Object *obj);
static bool check_canary(Object *obj);
static void register_region(Region *region);
static Region *new_region(size_t capacity);
static Object *heap_add_region(Region *region);
static Object *find_free_object(size_t size);
static Object *split_object(Object *obj, size_t size);
static Object *coalesce_object(Object *obj);

static size_t align_up(size_t n, size_t alignment)
{
        size_t remainder = n % alignment;

        if (remainder == 0)
                return n;

        size_t padding = alignment - remainder;

        if (n > SIZE_MAX - padding)
                return 0;

        return n + padding;
}

static size_t object_total_size(size_t requested_size)
{
        size_t payload_offset = align_up(sizeof(Object), _Alignof(max_align_t));

        if (payload_offset == 0)
                return 0;

        size_t overhead = payload_offset + CANARY_SIZE + sizeof(size_t);

        if (requested_size > SIZE_MAX - overhead)
                return 0;

        return align_up(requested_size + overhead, _Alignof(max_align_t));
}

static void write_canary(Object *obj)
{
        size_t payload_offset = align_up(sizeof(Object), _Alignof(max_align_t));
        uint64_t canary = CANARY_VALUE;
        unsigned char *location = (unsigned char *)obj + payload_offset + obj->requested_size;

        memcpy(location, &canary, sizeof(canary));
}

static void write_footer(Object *obj)
{
        unsigned char *end = (unsigned char *)obj + obj->size;

        memcpy(end - sizeof(size_t), &obj->size, sizeof(obj->size));
}

 
static bool check_canary(Object *obj)
{
        size_t payload_offset = align_up(sizeof(Object), _Alignof(max_align_t));

        if (payload_offset == 0 || obj->size < payload_offset)
                return false;

        size_t remaining = obj->size - payload_offset;

        if (remaining < CANARY_SIZE + sizeof(size_t))
                return false;

        if (obj->requested_size > remaining - CANARY_SIZE - sizeof(size_t))
                return false;

        uint64_t canary;

        memcpy(&canary,
               (unsigned char *)obj + payload_offset + obj->requested_size,
               sizeof(canary));

        return canary == CANARY_VALUE;
}


static void register_region(Region *region)
{
        region->next = heap.regions;
        heap.regions = region;
}

/* maps a new memory region */
static Region *new_region(size_t capacity)
{
        size_t alignment = _Alignof(max_align_t);
        size_t data_offset = align_up(sizeof(Region), alignment);

        if (data_offset == 0 || capacity > SIZE_MAX - data_offset)
                return NULL;

        size_t mapping_size = data_offset + capacity;
        Region *region = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
                              MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

        if (region == MAP_FAILED)
                return NULL;

        region->next = NULL;
        region->mapping_size = mapping_size;
        region->data_offset = data_offset;
        region->capacity = capacity;
        region->free_head = NULL;

        return region;
}

/* init and registers region */
static Object *heap_add_region(Region *region)
{
        if (region == NULL)
                return NULL;

        if (region->capacity < sizeof(Object) + sizeof(size_t))
                return NULL;

        Object *obj = (Object *)((unsigned char *)region + region->data_offset);

        obj->size = region->capacity;
        obj->requested_size = 0;
        obj->magic = OBJECT_MAGIC;
        obj->used = 0;
        obj->prev_free = NULL;
        obj->next_free = NULL;
        obj->parent_region = region;

        region->free_head = obj;
        write_footer(obj);
        register_region(region);

        return obj;
}

/* finds a free block large enough for the request */
static Object *find_free_object(size_t requested_size)
{
        if (requested_size == 0)
                return NULL;

        size_t required_size = object_total_size(requested_size);

        if (required_size == 0)
                return NULL;

        for (Region *region = heap.regions; region != NULL; region = region->next) {
                for (Object *obj = region->free_head; obj != NULL; obj = obj->next_free) {
                        if (!obj->used && obj->size >= required_size)
                                return obj;
                }
        }

        return NULL;
}

/* Use free for allocation */
static Object *split_object(Object *obj, size_t requested_size)
{
        if (obj == NULL || requested_size == 0)
                return NULL;

        size_t original_size = obj->size;
        size_t allocated_size = object_total_size(requested_size);

        if (allocated_size == 0 || original_size < allocated_size)
                return NULL;

        Region *region = obj->parent_region;
        size_t remainder_size = original_size - allocated_size;
        Object *old_prev = obj->prev_free;
        Object *old_next = obj->next_free;
        Object *allocated = obj;
        Object *remainder = (Object *)((unsigned char *)allocated + allocated_size);

        allocated->size = allocated_size;
        allocated->requested_size = requested_size;
        allocated->magic = OBJECT_MAGIC;
        allocated->used = 1;
        allocated->prev_free = NULL;
        allocated->next_free = NULL;
        allocated->parent_region = region;

        size_t min_free_size = align_up(sizeof(Object) + sizeof(size_t),
                                        _Alignof(max_align_t));

        if (remainder_size >= min_free_size) {
                remainder->size = remainder_size;
                remainder->requested_size = 0;
                remainder->used = 0;
                remainder->magic = OBJECT_MAGIC;
                remainder->prev_free = old_prev;
                remainder->next_free = old_next;
                remainder->parent_region = region;

                if (old_prev != NULL)
                        old_prev->next_free = remainder;
                else
                        region->free_head = remainder;

                if (old_next != NULL)
                        old_next->prev_free = remainder;

                write_footer(allocated);
                write_footer(remainder);
        } else {
                allocated->size = original_size;

                if (old_prev != NULL)
                        old_prev->next_free = old_next;
                else
                        region->free_head = old_next;

                if (old_next != NULL)
                        old_next->prev_free = old_prev;

                write_footer(allocated);
        }

        return allocated;
}

/* [FREE][FREE} -> [BIG FREE] */
static Object *coalesce_object(Object *obj)
{
    //Bun d*e basicly
        if (obj == NULL)
                return NULL;

        Region *region = obj->parent_region;
        unsigned char *begin = (unsigned char *)region + region->data_offset;
        unsigned char *end = begin + region->capacity;

        if ((unsigned char *)obj > begin) {
                size_t previous_size;

                memcpy(&previous_size,
                       (unsigned char *)obj - sizeof(size_t),
                       sizeof(previous_size));

                if (previous_size <= (size_t)((unsigned char *)obj - begin)) {
                        Object *previous = (Object *)((unsigned char *)obj - previous_size);

                        if (previous->magic == OBJECT_MAGIC && !previous->used) {
                                if (previous->prev_free != NULL)
                                        previous->prev_free->next_free = previous->next_free;
                                else
                                        region->free_head = previous->next_free;

                                if (previous->next_free != NULL)
                                        previous->next_free->prev_free = previous->prev_free;

                                previous->size += obj->size;
                                obj = previous;
                        }
                }
        }

        unsigned char *next_address = (unsigned char *)obj + obj->size;

        if (next_address < end) {
                Object *next = (Object *)next_address;

                if (next->magic == OBJECT_MAGIC && !next->used) {
                        if (next->prev_free != NULL)
                                next->prev_free->next_free = next->next_free;
                        else
                                region->free_head = next->next_free;

                        if (next->next_free != NULL)
                                next->next_free->prev_free = next->prev_free;

                        obj->size += next->size;
                }
        }

        write_footer(obj);
        return obj;
}

/* allocate */
void *tm_alloc(size_t size)
{
        if (size == 0)
                return NULL;

        size_t required_size = object_total_size(size);

        if (required_size == 0)
                return NULL;

        Object *obj = find_free_object(size);

        if (obj == NULL) {
                size_t capacity = required_size;

                if (capacity < REGION_DEFAULT_CAPACITY)
                        capacity = REGION_DEFAULT_CAPACITY;

                Region *region = new_region(capacity);

                if (region == NULL)
                        return NULL;

                if (heap_add_region(region) == NULL) {
                        munmap(region, region->mapping_size);
                        return NULL;
                }

                obj = region->free_head;

                if (obj == NULL || obj->size < required_size)
                        return NULL;
        }

        Object *allocated = split_object(obj, size);

        if (allocated == NULL)
                return NULL;

        write_canary(allocated);

        size_t payload_offset = align_up(sizeof(Object), _Alignof(max_align_t));

        if (payload_offset == 0)
                return NULL;

        return (unsigned char *)allocated + payload_offset;
}

/* Let go of allocation */
void tm_free(void *ptr)
{
        if (ptr == NULL)
                return;

        size_t payload_offset = align_up(sizeof(Object), _Alignof(max_align_t));

        if (payload_offset == 0)
                return;

        Object *obj = (Object *)((unsigned char *)ptr - payload_offset);

        if (obj->magic != OBJECT_MAGIC || !obj->used)
                return;

        if (!check_canary(obj))
                return;

        Region *region = obj->parent_region;

        if (region == NULL)
                return;

        obj->used = 0;
        obj->requested_size = 0;

        obj = coalesce_object(obj);

        if (obj == NULL)
                return;

        obj->prev_free = NULL;
        obj->next_free = region->free_head;

        if (region->free_head != NULL)
                region->free_head->prev_free = obj;

        region->free_head = obj;
        write_footer(obj);
}

#endif
#endif
