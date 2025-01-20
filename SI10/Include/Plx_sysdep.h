#ifndef _PLX_SYSDEP_H_
#define _PLX_SYSDEP_H_

/******************************************************************************
 *
 * File Name:
 *
 *      Plx_sysdep.h
 *
 * Description:
 *
 *      This file is provided to support compatible code between different
 *      Linux kernel versions.
 *
 * Revision History:
 *
 *      08-01-07 : PLX SDK v5.20
 *
 *****************************************************************************/


#ifndef LINUX_VERSION_CODE
    #include <linux/version.h>
#endif


// Only allow 2.4 and 2.6 kernels
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0)
    #error "Linux kernel versions before v2.4 not supported"
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,7,0)
    #error "Linux kernel versions greater than v2.6 not supported"
#endif


// Detect current version
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
    #define LINUX_24
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,7,0)
    #define LINUX_26
#else
    #error "Unable to determine Linux kernel support"
#endif


// Missing definitions in 2.4
#if defined(LINUX_24)
    #define __user
    #define __GFP_NOWARN                0
    #define DMA_TO_DEVICE               1
    #define DMA_FROM_DEVICE             2
    #define iminor(inode)               MINOR((inode)->i_rdev)
    #define imajor(inode)               MAJOR((inode)->i_rdev)
    #define pgprot_noncached(x)         (x)

    // ISR returns nothing in kernel 2.4
    #define irqreturn_t                 void
    #define PLX_IRQ_RETVAL(value)
#else
    #define PLX_IRQ_RETVAL              IRQ_RETVAL
#endif




/***********************************************************
 * INIT_WORK
 *
 * This macro initializes a work structure with the function
 * to call.  In kernel 2.6.20, the 3rd parameter was removed.
 * It used to be the parameter to the function, but now, the
 * function is called with a pointer to the work_struct itself.
 **********************************************************/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
    #define PLX_INIT_WORK                     INIT_WORK
#else
    #define PLX_INIT_WORK(work, func, data)   INIT_WORK((work), (func))
#endif




/***********************************************************
 * PLX_DPC_PARAM
 *
 * In kernel 2.6.20, the parameter to a work queue function
 * was made to always be a pointer to the work_struct itself.
 * In previous kernels, this was always a VOID*.  Since
 * PLX drivers use work queue functions for the DPC/bottom-half
 * processing, the parameter had to be changed.  For cleaner
 * source code, the definition PLX_DPC_PARAM is used and is
 * defined below.  This also allows 2.4.x compatible source code.
 **********************************************************/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
    #define PLX_DPC_PARAM      VOID
#else
    #define PLX_DPC_PARAM      struct work_struct
#endif




/***********************************************************
 * SA_SHIRQ / IRQF_SHARED
 *
 * In kernel 2.6.18, the IRQ flag SA_SHIRQ was renamed to
 * IRQF_SHARED.  SA_SHIRQ was deprecated but remained in the
 * the kernel headers to support older drivers until 2.6.24.
 **********************************************************/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
    #define PLX_IRQF_SHARED    SA_SHIRQ
#else
    #define PLX_IRQF_SHARED    IRQF_SHARED
#endif




/***********************************************************
 * dma_set_mask
 *
 * This function is used to set the mask for DMA addresses
 * for the device.  In 2.4 kernels, the function is pci_set_dma_mask
 * and in 2.6 kernels, it has been change to dma_set_mask.
 * In addition to the name change, the first parameter has
 * been changed from the PCI device structure in 2.4, to
 * the device structure found in the PCI device structure.
 **********************************************************/
#if defined(LINUX_24)
    #define Plx_dma_set_mask(pdx, mask) \
            (                           \
                pci_set_dma_mask(       \
                    (pdx)->pPciDevice,  \
                    (mask)              \
                    )                   \
            )
#else
    #define Plx_dma_set_mask(pdx, mask)        \
            (                                  \
                dma_set_mask(                  \
                    &((pdx)->pPciDevice->dev), \
                    (mask)                     \
                    )                          \
            )
#endif




/***********************************************************
 * dma_alloc_coherent & dma_free_coherent
 *
 * These functions are used to allocate and map DMA buffers.
 * In 2.4 kernels, the functions are pci_alloc/free_consistent
 * and in 2.6 kernels, they have been changed to
 * dma_alloc/free_coherent.  In addition to the name changes,
 * the first parameter has been changed from the PCI device
 * structure in 2.4, to the device structure found in the PCI
 * device structure.
 **********************************************************/
#if defined(LINUX_24)
    #define Plx_dma_alloc_coherent(pdx, size, dma_handle, flag) \
                pci_alloc_consistent(  \
                    (pdx)->pPciDevice, \
                    (size),            \
                    (dma_handle)       \
                    )

    #define Plx_dma_free_coherent(pdx, size, cpu_addr, dma_handle) \
                pci_free_consistent(   \
                    (pdx)->pPciDevice, \
                    (size),            \
                    (cpu_addr),        \
                    (dma_handle)       \
                    )
#else
    #define Plx_dma_alloc_coherent(pdx, size, dma_handle, flag) \
                dma_alloc_coherent(            \
                    &((pdx)->pPciDevice->dev), \
                    (size),                    \
                    (dma_handle),              \
                    (flag)                     \
                    )

    #define Plx_dma_free_coherent(pdx, size, cpu_addr, dma_handle) \
                dma_free_coherent(             \
                    &((pdx)->pPciDevice->dev), \
                    (size),                    \
                    (cpu_addr),                \
                    (dma_handle)               \
                    )
#endif




/***********************************************************
 * dma_map_page & dma_unmap_page
 *
 * These functions are used to map a single user buffer page
 * in order to get a valid bus address for the page. In 2.4
 * kernels, the functions are pci_map/unmap_page and in 2.6
 * kernels, they have been changed to dma_map/unmap_page.
 * In addition to the name changes, the first parameter has
 * been changed from the PCI device structure in 2.4, to the
 * device structure found in the PCI device structure.
 **********************************************************/
#if defined(LINUX_24)
    #define Plx_dma_map_page(pdx, page, offset, size, direction) \
                pci_map_page(          \
                    (pdx)->pPciDevice, \
                    (page),            \
                    (offset),          \
                    (size),            \
                    (direction)        \
                    )

    #define Plx_dma_unmap_page(pdx, dma_address, size, direction) \
                pci_unmap_page(        \
                    (pdx)->pPciDevice, \
                    (dma_address),     \
                    (size),            \
                    (direction)        \
                    )
#else
    #define Plx_dma_map_page(pdx, page, offset, size, direction) \
                dma_map_page(                  \
                    &((pdx)->pPciDevice->dev), \
                    (page),                    \
                    (offset),                  \
                    (size),                    \
                    (direction)                \
                    )

    #define Plx_dma_unmap_page(pdx, dma_address, size, direction) \
                dma_unmap_page(                \
                    &((pdx)->pPciDevice->dev), \
                    (dma_address),             \
                    (size),                    \
                    (direction)                \
                    )
#endif




/***********************************************************
 * remap_pfn_range & remap_page_range
 *
 * The remap_pfn_range() function was added in kernel 2.6 and
 * does not exist in previous kernels.  For older kernels,
 * remap_page_range can be used, as it is the same function
 * except the Page Frame Number (pfn) parameter should
 * actually be a physical address.  For that case, the
 * pfn is simply shifted by PAGE_SHIFT to obtain the
 * corresponding physical address.
 *
 * remap_pfn_range, however, does not seem to exist in all
 * kernel 2.6 distributions.  remap_page_range was officially
 * removed in 2.6.11.  To keep things simple, this driver
 * will default to using remap_page_range unless the kernel
 * version is 2.6.11 or greater.
 *
 * For 2.4 kernels, remap_pfn_range obviously does not exist.
 * Although remap_page_range() may be used instead, there
 * was a parameter added in kernel version 2.5.3. The new
 * parameter is a pointer to the VMA structure.  To make
 * matters even more complicated, the kernel source in
 * RedHat 9.0 (v2.4.20-8), however, also uses the new
 * parameter.  As a result, another #define is added if
 * RedHat 9.0 kernel source is used.
 *
 * The #defines below should result in the following usage table:
 *
 *  kernel                        function
 * ====================================================
 *  2.4.0  -> 2.4.19              remap_page_range (no VMA param)
 *  2.4.20 -> 2.5.2  (non-RedHat) remap_page_range (no VMA param)
 *  2.4.20 -> 2.5.2  (RedHat)     remap_page_range (with VMA param)
 *  2.5.3  -> 2.6.10              remap_page_range (with VMA param)
 *  2.6.11 & up                   remap_pfn_range
 *
 **********************************************************/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11))
    #if ( (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,3)) || \
         ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,20)) && defined(RED_HAT_LINUX_KERNEL)) )

        // Revert to remap_page_range
        #define Plx_remap_pfn_range(vma, virt_addr, pfn, size, prot) \
                (                           \
                    remap_page_range(       \
                        (vma),              \
                        (virt_addr),        \
                        (pfn) << PAGE_SHIFT,\
                        (size),             \
                        (prot)              \
                        )                   \
                )
    #else
        // VMA parameter must be removed
        #define Plx_remap_pfn_range(vma, virt_addr, pfn, size, prot) \
                (                           \
                    remap_page_range(       \
                        (virt_addr),        \
                        (pfn) << PAGE_SHIFT,\
                        (size),             \
                        (prot)              \
                        )                   \
                )
    #endif
#else
    // Function already defined
    #define Plx_remap_pfn_range         remap_pfn_range
#endif




/***********************************************************
 * io_remap_pfn_range & io_remap_page_range
 *
 * The io_remap_page_range() function is used to map I/O space
 * into user mode.  Generally, it defaults to remap_page_range,
 * but on some architectures it performs platform-specific code.
 *
 * In kernel 2.6.12, io_remap_page_range was deprecated and
 * replaced with io_remap_pfn_range.
 *
 * Since io_remap_xxx_range usually reverts to remap_xxx_range,
 * the same issues regarding kernel version apply.  Refer to
 * the explanation above regarding remap_page/pfn_range.
 *
 * The #defines below should result in the following usage table:
 *
 *  kernel                        function
 * ====================================================
 *  2.4.0  -> 2.4.19              io_remap_page_range (no VMA param)
 *  2.4.20 -> 2.5.2  (non-RedHat) io_remap_page_range (no VMA param)
 *  2.4.20 -> 2.5.2  (RedHat)     io_remap_page_range (with VMA param)
 *  2.5.3  -> 2.6.11              io_remap_page_range (with VMA param)
 *  2.6.12 & up                   io_remap_pfn_range
 *
 **********************************************************/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12))
    #if ( (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,3)) || \
         ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,20)) && defined(RED_HAT_LINUX_KERNEL)) )

        // Revert to io_remap_page_range (with VMA)
        #define Plx_io_remap_pfn_range(vma, virt_addr, pfn, size, prot) \
                (                           \
                    io_remap_page_range(    \
                        (vma),              \
                        (virt_addr),        \
                        (pfn) << PAGE_SHIFT,\
                        (size),             \
                        (prot)              \
                        )                   \
                )
    #else
        // Revert to io_remap_page_range (without VMA)
        #define Plx_io_remap_pfn_range(vma, virt_addr, pfn, size, prot) \
                (                           \
                    io_remap_page_range(    \
                        (virt_addr),        \
                        (pfn) << PAGE_SHIFT,\
                        (size),             \
                        (prot)              \
                        )                   \
                )
    #endif
#else
    // Function already defined
    #define Plx_io_remap_pfn_range      io_remap_pfn_range
#endif




/**********************************************************
 * The following implements an interruptible wait_event
 * with a timeout for older kernels.  This is used instead
 * of the function interruptible_sleep_on_timeout() since
 * this is susceptible to race conditions.
 *
 *    retval == 0; timed out
 *    retval >  0; condition met or task signaled
 *                 ret=jiffies remaining before timeout
 *    retval <  0; error condition or interrupted by signal
 *********************************************************/
#if defined(LINUX_24)
    #define Plx_wait_event_interruptible_timeout(wq, condition, timeout) \
    ({                                                   \
        int __ret = timeout;                             \
                                                         \
        if (!(condition))                                \
        {                                                \
            __Plx__wait_event_interruptible_timeout(     \
                wq,                                      \
                condition,                               \
                __ret                                    \
                );                                       \
        }                                                \
        __ret;                                           \
    })


    #define __Plx__wait_event_interruptible_timeout(wq, condition, ret) \
    do                                               \
    {                                                \
        wait_queue_t __wait;                         \
                                                     \
        init_waitqueue_entry(&__wait, current);      \
                                                     \
        add_wait_queue(&wq, &__wait);                \
        for (;;)                                     \
        {                                            \
            set_current_state(TASK_INTERRUPTIBLE);   \
            if (condition)                           \
                break;                               \
            if (!signal_pending(current))            \
            {                                        \
                ret = schedule_timeout(ret);         \
                if (!ret)                            \
                    break;                           \
                continue;                            \
            }                                        \
            ret = -ERESTARTSYS;                      \
            break;                                   \
        }                                            \
        current->state = TASK_RUNNING;               \
        remove_wait_queue(&wq, &__wait);             \
    }                                                \
    while (0)

#else
    #define Plx_wait_event_interruptible_timeout     wait_event_interruptible_timeout
#endif



#endif  // _PLX_SYSDEP_H_
