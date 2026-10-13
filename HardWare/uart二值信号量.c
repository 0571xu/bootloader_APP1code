//=======================================
//初始化IO 串口1 
//bound:波特率
//=======================================
#include "uart.h"
#include "stm32f10x.h"
// #include "sys.h"
// #include "stdio.h"	

// #if 1
// #pragma import(__use_no_semihosting)             
// //标准库需要的支持函数                 
// struct __FILE 
// { 
// 	int handle; 

// }; 

// FILE __stdout;       
// //定义_sys_exit()以避免使用半主机模式    
// _sys_exit(int x) 
// { 
// 	x = x; 
// } 
// //重定义fputc函数 
// int fputc(int ch, FILE *f)
// {      
// 	while((USART1->SR&0X40)==0);//循环发送,直到发送完毕   
//     USART1->DR = (u8) ch;      
// 	return ch;
// }
// #endif 

uint8_t Recv[100];
uint8_t rx_cnt = 0;
SemaphoreHandle_t uartSemaphore = NULL;


void uart_init(u32 bound){
    //GPIO端口设置
    GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	 
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1|RCC_APB2Periph_GPIOA|RCC_APB2Periph_AFIO, ENABLE);	//使能USART1，GPIOA时钟以及复用功能时钟
     //USART1_TX   PA.9
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9; //PA.9
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;	//复用推挽输出
    GPIO_Init(GPIOA, &GPIO_InitStructure);
   
    //USART1_RX	  PA.10
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;//浮空输入
    GPIO_Init(GPIOA, &GPIO_InitStructure);  

   //Usart1 NVIC 配置

    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority=3 ;//抢占优先级3
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;		//子优先级3
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;			//IRQ通道使能
	NVIC_Init(&NVIC_InitStructure);	//根据指定的参数初始化VIC寄存器
  
   //USART 初始化设置

	USART_InitStructure.USART_BaudRate = bound;//一般设置为9600;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;//字长为8位数据格式
	USART_InitStructure.USART_StopBits = USART_StopBits_1;//一个停止位
	USART_InitStructure.USART_Parity = USART_Parity_No;//无奇偶校验位
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;//无硬件数据流控制
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;	//收发模式

    USART_Init(USART1, &USART_InitStructure); //初始化串口
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);//开启中断
    USART_Cmd(USART1, ENABLE);                    //使能串口 

#if EN_USART1_RX	
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);//开启相关中断
	USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);
    
#endif
}

//=======================================
//串口1中断服务程序
//=======================================
void USART1_IRQHandler(void)                	
{
	uint8_t data;//接收数据暂存变量
	BaseType_t xHigherPriorityTaskWoken;

	if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)  //接收中断
	{
		data =USART_ReceiveData(USART1);   			
		Recv[rx_cnt++]=data;//接收的数据存入接收数组 
		
		USART_ClearITPendingBit(USART1,USART_IT_RXNE);
	} 
	
	if(USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)//空闲中断
	{
		if(uartSemaphore!=NULL)
		{
			//释放二值信号量
			xSemaphoreGiveFromISR(uartSemaphore,&xHigherPriorityTaskWoken);	//释放二值信号量
		}
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);//如果需要的话进行一次任务切换
		
		data = USART1->SR;//串口空闲中断的中断标志只能通过先读SR寄存器，再读DR寄存器清除！
		data = USART1->DR;
		//USART_ClearITPendingBit(USART1,USART_IT_IDLE);//这种方式无效
	
		//rx_cnt=0;
	}
} 

//打印任务函数
void print_task(void *pvParameters)
{
	int count=0;
	BaseType_t err = pdFALSE;
	int i;

	int size=50;
	uint8_t buf[64];//最多只取前64个数据

	//清空本地接收数组
	memset(buf,0,size);
	
	while(1)
	{
		err=xSemaphoreTake(uartSemaphore,10);	//获取信号量
		if(err==pdTRUE)							//获取信号量成功
		{  
			//printf("%s",Data);
			if(rx_cnt < size)//收到的数据长度在size范围内
			{
				//void *memcpy(void *str1, const void *str2, size_t n)  
				//从存储区 str2 复制 n 个字节到存储区 str1。
				memcpy(buf,Recv,rx_cnt);//有几个复制几个
				count=rx_cnt;
				//printf("%s\r\n", buf);
			}
			else//收到的数据长度太长了
			{
				memcpy(buf,Recv,size);//只复制size个
				count=size;
			}
			rx_cnt=0;
		}
		
		if(count>0)
		{
			for(i = 0; i<count; i++)
			{
				while((USART1->SR & 0x40) == 0);   // ★ 等 TXE
				USART1->DR = buf[i];
			}
			while((USART1->SR & 0x40) == 0);
            USART1->DR = '\r';
            while((USART1->SR & 0x40) == 0);
            USART1->DR = '\n';
			count=0;
			
			
			//------------------------------------------------------------------------------
			//这里可以继续对buf进行分析和处理，比如根据buf的不同内容执行不同的小任务

		}
	}
}


