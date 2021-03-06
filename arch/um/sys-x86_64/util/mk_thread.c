#include <stdio.h>
#include <kernel-offsets.h>

int main(int argc, char **argv)
{
  printf("/*\n");
  printf(" * Generated by mk_thread\n");
  printf(" */\n");
  printf("\n");
  printf("#ifndef __UM_THREAD_H\n");
  printf("#define __UM_THREAD_H\n");
  printf("\n");
#ifdef TASK_EXTERN_PID
  printf("#define TASK_EXTERN_PID(task) *((int *) &(((char *) (task))[%d]))\n",
	 TASK_EXTERN_PID);
#endif
  printf("\n");
  printf("#endif\n");
  return(0);
}
