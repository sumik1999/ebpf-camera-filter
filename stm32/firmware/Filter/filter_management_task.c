#include "filter_management_task.h"

#include <stddef.h>

static void lock(filter_management_task_t *task)
{
    if (task->manager && task->manager->enter_critical)
        task->manager->enter_critical(task->manager->critical_opaque);
}
static void unlock(filter_management_task_t *task)
{
    if (task->manager && task->manager->exit_critical)
        task->manager->exit_critical(task->manager->critical_opaque);
}

void filter_management_task_init(filter_management_task_t *task,
                                 filter_manager_t *manager,
                                 filter_mgmt_idle_fn idle,
                                 filter_mgmt_result_fn result,
                                 void *callback_opaque)
{
    if (task==NULL) return;
    task->manager=manager; task->head=0; task->tail=0; task->count=0; task->stop=0;
    task->idle=idle; task->result=result; task->callback_opaque=callback_opaque;
    for (uint32_t i=0;i<FILTER_MGMT_QUEUE_DEPTH;i++) {
        task->queue[i].len=0; task->queue[i].state=0;
    }
}

int filter_management_submit(filter_management_task_t *task,
                             const uint8_t *message, uint32_t len)
{
    if (task==NULL||message==NULL||len==0u||len>FILTER_UPDATE_MAX_MSG) return 0;
    lock(task);
    if (task->count==FILTER_MGMT_QUEUE_DEPTH) {
        unlock(task);
        return 0;
    }
    filter_mgmt_message_t *item=&task->queue[task->tail];
    item->state=1u;
    task->tail=(uint8_t)((task->tail+1u)%FILTER_MGMT_QUEUE_DEPTH);
    task->count++;
    unlock(task);

    item->len=(uint16_t)len;
    for (uint32_t i=0;i<len;i++) item->data[i]=message[i];
    lock(task); item->state=2u; unlock(task);
    return 1;
}

int filter_management_process_one(filter_management_task_t *task)
{
    if (task==NULL||task->manager==NULL) return 0;
    lock(task);
    if (task->count==0u || task->queue[task->head].state!=2u) {
        unlock(task);
        return 0;
    }
    filter_mgmt_message_t *item=&task->queue[task->head];
    item->state=3u;
    unlock(task);

    vm_verify_result_t why=VM_VERIFY_OK;
    filter_update_result_t result=filter_manager_install(task->manager,item->data,item->len,&why);
    lock(task);
    if (result==FILTER_UPDATE_ERR_STORAGE) {
        item->state=2u; /* transient RX reference: retry the same FIFO head */
        unlock(task);
        return 0;
    }
    item->state=0u;
    task->head=(uint8_t)((task->head+1u)%FILTER_MGMT_QUEUE_DEPTH);
    task->count--;
    unlock(task);
    if (task->result) task->result(task->callback_opaque,result,why);
    return 1;
}

void filter_management_task(void *argument)
{
    filter_management_task_t *task=(filter_management_task_t *)argument;
    if (task==NULL) return;
    while (!task->stop) {
        if (!filter_management_process_one(task) && task->idle) {
            task->idle(task->callback_opaque);
        }
    }
}
