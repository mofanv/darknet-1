#include <stdio.h>
#include <math.h>
/*
*  将输入图像im的channel通道上的第row行，col列像素灰度值加上val（直接修改im的值，因此im相当于是返回值）
** 输入： im         输入图像
**       channels   输入图像的im通道数（这个参数没用。。。）
**       height     输入图像im的高度（行）
**       width      输入图像im的宽度（列）
**       row        需要加上val的像素所在的行数（补零之后的行数，因此需要先减去pad才能得到真正在im中的行数）
**       col        需要加上val的像素所在的列数（补零之后的列数，因此需要先减去pad才能得到真正在im中的列数）
**       channel    需要加上val的像素所在的通道数
**       pad        四周补0长度
**       val        像素灰度添加值
*/
void col2im_add_pixel(float *im, int height, int width, int channels,
                        int row, int col, int channel, int pad, float val)
{
    row -= pad;
    col -= pad;

    // 边界检查：超过边界则不作为返回
    if (row < 0 || col < 0 ||
        row >= height || col >= width) return;
    im[col + width*(row + height*channel)] += val;
}
/*
** 此函数与im2col_cpu()函数的流程相反，目地是将im2col_cpu()函数重排得到的图片data_col恢复至正常的图像矩阵排列，并与data_im相加，最终data_im相当于是输出值，
** 要注意的是，data_im的尺寸是在函数外确定的，且并没有显示的将data_col转为一个与data_im尺寸相同的矩阵，而是将其中元素直接加在data_im对应元素上（data_im初始所有元素值都为0）。
** 得到的data_im尺寸为l.c*l.h*l.w，即为当前层的输入图像尺寸，上一层的输出图像尺寸，按行存储，可视为l.c行，l.h*l.w列，即其中每行对应一张输出特征图的敏感度图（实际上这还不是最终的敏感度，
** 还差一个环节：乘以激活函数对加权输入的导数，这将在下一次调用backward_convolutional_laye时完成）。
**
** 举个例子：
** 第L-1层每张输入图片（本例子只分析单张输入图片）的输出为5*5*3（3为输出通道数），第L层共有2个卷积核，每个卷积核的尺寸为3*3，stride = 2,
** 第L-1层的输出是第L层的输入，第L层的2个卷积核同时对上一层3个通道的输出做卷积运算，为了做到这一点，需要调用im2col_cpu()函数将
** 上一层的输出，也就是本层的输入重排为27行4列的图，也就是由5*5*3变换至27*4，你会发现总的元素个数变多了（75增多到了98），
** 这是因为卷积核stride=2,小于卷积核的尺寸3,因此卷积在两个连续位置做卷积，会有重叠部分，而im2col_cpu()函数为了便于卷积运算，完全将其
** 铺排开来，并没有在空间上避免重复元素，因此像素元素会增多。此外，之所以是27行，是因为卷积核尺寸为3*3，而上一层的输出即本层输入有3个通道，
** 为了同时给3个通道做卷积运算，需要将3个通道上的输入一起考虑，即得到3*3*3行，4列是因为对于对于5*5的图像，使用3*3的卷积核，stride=2的卷积跨度，
** 最终会得到2*2的特征图，也就是4个元素。除了调用im2col_cpu()对输入图像做重排，相应的，也要将所有卷积核重排成一个2*27的矩阵，为什么是2呢？
** 因为有两个卷积核，为了做到同时将两个卷积核作用到输入图像上，需要将两个核合到一个矩阵中，每个核对应一行，因此有2行，那为什么是27呢？每个核
** 元素个数不是3*3=9吗？是的，但是考虑到要同时作用到3个通道上，所以实际一个卷积核有9*3=27个元素。综述，得到2*27的卷积核矩阵与27*4的输入图像矩阵，
** 两个矩阵相乘，即可完成将2个卷积核同时作用于3通道的输入图像上（非常方便，不枉前面非这么大劲的重排！），最终得到2*4的矩阵，这2*4矩阵又代表这什么呢？
** 2代表这有两个输出图（对应2个卷积核，即l.out_c=2），每个输出图占一行，4代表这每个输出图元素有4个（前面说了，每个卷积核会得到2*2的特征图，即l.out_h=l.out_w=2）。这个例子说到这，只说完了
** 前向传播部分，可以看出im2col_cpu()这个函数的重要性。而此处的col2im_cpu()是一个逆过程，主要用于反向传播中，由L层的敏感度图(sensitivity map，
** 可能每个地方叫的不一样，此处参考博客：https://www.zybuluo.com/hanbingtao/note/485480)反向求得第L-1层的敏感度图。顺承上面的例子，第L-1层的输出
** 是一个5*5*3（l.w=l.h=5,l.c=3）的矩阵，也就是敏感度图的维度为5*5*3（每个输出元素，对应一个敏感度值），第L层的输出是一个2*4的矩阵，敏感度图的维度为2*4，假设已经计算得到了
** 第L层的2*4的敏感度图，那么现在的问题是，如何由第L层的2*4敏感度图以及2个卷积核（2*27）反向获取第L-1层的敏感度图呢？上面给的博客链接给出了一种很好的求解方式，
** 但darknet并不是这样做的，为什么？因为前面有im2col_cpu()，im2col_cpu()函数中的重排方式，使得我们不再需要博客中提到的将sensitivity map还原为步长为1的sensitivity map，
** 只需再使用col2im_cpu()就可以了！过程是怎样的呢，看backward_convolutional_layer()函数中if(net.delta)中的语句就知道了，此处仅讨论col2im_cpu()的过程，
** 在backward_convolutional_layer()已经得到了data_col，这个矩阵含有了所有的第L-1层敏感度的信息，但遗憾的是，不能直接用，需要整理，因为此时的data_col还是一个
** 27*4的矩阵，而我们知道第L-1层的敏感度图是一个5*5*3的矩阵，如何将一个27*4的矩阵变换至一个5*5*3的矩阵是本函数要完成的工作，前面说到27*4元素个数多于5*5*3,
** 很显然要从27*4变换至5*5*3，肯定会将某些元素相加合并（下面col2im_add_pixel()函数就是干这个的），具体怎样，先不说，先来看看输入参数都代表什么意思吧：
** 输入： data_col    backward_convolutional_layer()中计算得到的包含上一层所有敏感度信息的矩阵，行数为l.n*l.size*l.size（l代表本层/当前层），列数为l.out_h*l.out_w（对于本例子，行数为27,列数为4,上一层为第L-1层，本层是第L层） 
**       channels    当前层输入图像的通道数（对于本例子，为3）
**       height      当前层输入图像的行数（对于本例子，为5）
**       width       当前层输入图像的列数（对于本例子，为5）
**       ksize       当前层卷积核尺寸（对于本例子，为3）
**       stride      当前层卷积跨度（对于本例子，为2）
**       pad         当前层对输入图像做卷积时四周补0的长度
**       data_im     经col2im_cpu()重排恢复之后得到的输出矩阵，也即上一层的敏感度图，尺寸为l.c * l.h * l.w（刚好就是上一层的输出当前层输入的尺寸，对于本例子，5行5列3通道），
**                   注意data_im的尺寸，是在本函数之外就已经确定的，不是在本函数内部计算出来的，这与im2col_cpu()不同，im2col_cpu()计算得到的data_col的尺寸都是在函数内部计算得到的，
**                   并不是事先指定的。也就是说，col2im_cpu()函数完成的是指定尺寸的输入矩阵往指定尺寸的输出矩阵的转换。
** 原理：原理比较复杂，很难用文字叙述，博客：https://www.zybuluo.com/hanbingtao/note/485480中基本原理说得很详细了，但是此处的实现与博客中并不一样，所以具体实现的原理此处简要叙述一下，具体见个人博客。
**      第L-1层得到l.h*l.w*l.c输出，也是第L层的输入，经L层卷积及激活函数处理之后，得到l.out_h*l.out_w*l.out_c的输出，也就是由l.h*l.w*l.c-->l.out_h*l.out_w*l.out_c，
**      由于第L层有多个卷积核，所以第L-1层中的一个输出元素会流入到第L层多个输出中，除此之外，由于卷积核之间的重叠，也导致部分元素流入到第L层的多个输出中，这两种情况，都导致第L-1层中的某个敏感度会与第L层多个输出有关，
**      为清晰，还是用上面的例子来解释，第L-1层得到5*5*3(3*25)的输出，第L层得到2*2*2（2*4）的输出，在backward_convolutional_layer()已经计算得到的data_col实际是27*2矩阵与2*4矩阵相乘的结果，
**      为方便，我们记27*2的矩阵为a，记2*4矩阵为b，那么a中一行（2个元素）与b中一列（2个元素）相乘对应这什么呢？对应第一情况，因为有两个卷积核，使得L-1中一个输出至少与L层中两个输出有关系，经此矩阵相乘，得到27*4的矩阵，
**      已经考虑了第一种情况（27*4这个矩阵中的每一个元素都是两个卷积核影响结果的求和），那么接下来的就是要考虑第二种情况：卷积核重叠导致的一对多关系，具体做法就是将data_col中对应相同像素的值相加，这是由
**      im2col_cpu()函数决定的（可以配合im2col_cpu()来理解），因为im2col_cpu()将这些重叠元素也铺陈保存在data_col中，所以接下来，只要按照im2col_cpu()逆向将这些重叠元素的影响叠加就可以了，
**      大致就是这个思路，具体的实现细节可能得见个人博客了（这段写的有点罗嗦～）。       
*/
void col2im_cpu(float* data_col,
         int channels,  int height,  int width,
         int ksize,  int stride, int pad, float* data_im) 
{
    int c,h,w;
    // 当前层输出图的尺寸（对于上面的例子，height_col=2,width_col=2）
    int height_col = (height + 2*pad - ksize) / stride + 1;
    int width_col = (width + 2*pad - ksize) / stride + 1;

    // 当前层每个卷积核在所有输入图像通道上的总元素个数（对于上面的例子，channels_col=3*3*3=27）
    // 注意channels_col实际是data_col的行数
    int channels_col = channels * ksize * ksize;

    // 开始遍历：外循环遍历data_col的每一行（对于上面的例子，data_col共27行）
    for (c = 0; c < channels_col; ++c) {

        // 列偏移，卷积核是一个二维矩阵，并按行存储在一维数组中，利用求余运算获取对应在卷积核中的列数，比如对于
        // 3*3的卷积核，当c=0时，显然在第一列，当c=5时，显然在第2列，当c=9时，在第二通道上的卷积核的第一列
        int w_offset = c % ksize;

        // 行偏移，卷积核是一个二维的矩阵，且是按行（卷积核所有行并成一行）存储在一维数组中的，
        // 比如对于3*3的卷积核，处理3通道的图像，那么一个卷积核具有27个元素，每9个元素对应一个通道上的卷积核（互为一样），
        // 每当c为3的倍数，就意味着卷积核换了一行，h_offset取值为0,1,2
        int h_offset = (c / ksize) % ksize;

        // 通道偏移，channels_col是多通道的卷积核并在一起的，比如对于3通道，3*3卷积核，每过9个元素就要换一通道数，
        // 当c=0~8时，c_im=0;c=9~17时，c_im=1;c=18~26时，c_im=2
        // c_im是data_im的通道数（即上一层输出当前层输入的通道数），对于上面的例子，c_im取值为0,1,2
        int c_im = c / ksize / ksize;

        // 中循环与内循环和起来刚好遍历data_col的每一行（对于上面的例子，data_col的列数为4,height_col*width_col=4）
        for (h = 0; h < height_col; ++h) {
            for (w = 0; w < width_col; ++w) {

                // 获取在输出data_im中的行数im_row与列数im_col
                // 由上面可知，对于3*3的卷积核，h_offset取值为0,1,2,当h_offset=0时，会提取出所有与卷积核第一行元素进行运算的像素，
                // 依次类推；加上h*stride是对卷积核进行行移位操作，比如卷积核从图像(0,0)位置开始做卷积，那么最先开始涉及(0,0)~(3,3)
                // 之间的像素值，若stride=2，那么卷积核进行行移位一次时，下一行的卷积操作是从元素(2,0)（2为图像行号，0为列号）开始
                int im_row = h_offset + h * stride;
                // 对于3*3的卷积核，w_offset取值也为0,1,2，当w_offset取1时，会提取出所有与卷积核中第2列元素进行运算的像素，
                // 实际在做卷积操作时，卷积核对图像逐行扫描做卷积，加上w*stride就是为了做列移位，
                // 比如前一次卷积其实像素元素为(0,0)，若stride=2,那么下次卷积元素起始像素位置为(0,2)（0为行号，2为列号）
                int im_col = w_offset + w * stride;

                // 计算在输出data_im中的索引号
                // 对于上面的例子，im_row的取值范围为0~4,im_col从0~4，c从0~2（其中h_offset从0~2,w_offset从0~2, h从0~1,w从0~1）
                // 输出的data_im的尺寸为l.c * l.h * lw，对于上面的例子，为3*5*5,因此，im_row,im_col,c的取值范围刚好填满data_im

                // 获取data_col中索引为col_index的元素，对于上面的例子，data_col为27*4行，按行存储
                // col_index = c * height_col * width_col + h * width_col + w逐行读取data_col中的每一个元素。
                // 相同的im_row,im_col与c_im可能会对应多个不同的col_index，这就是卷积核重叠带来的影响，处理的方式是将这些val都加起来，
                // 存在data_im的第im_row - pad行，第im_col - pad列（c_im通道上）中。
                // 比如上面的例子，上面的例子，如果固定im_row = 0, im_col =2, c_im =0，由c_im = 0可以知道c在0~8之间，由im_row=0,可以确定h = 0, h_offset =0，
                // 可以得到两组：1)w_offset = 0, w = 1; 2) w_offset = 2, w =0，第一组，则可以完全定下：c=0,h=0,w=1，此时col_index=1，由第二组，可完全定下：c=2,h=0,w=0，
                // 此时col_index = 2*2*2=8
                int col_index = (c * height_col + h) * width_col + w;
                double val = data_col[col_index];

                // 从data_im找出c_im通道上第im_row - pad行im_col - pad列处的像素，使其加上val
                // height, width, channels都是上一层输出即当前层输入图像的尺寸，也是data_im的尺寸（对于本例子，三者的值分别为5,5,3）,
                // im_row - pad,im_col - pad,c_im都是某一具体元素在data_im中的行数与列数与通道数（因为im_row与im_col是根据卷积过程计算的，
                // 所以im_col和im_row中实际还包含了补零长度pad，需要减去之后，才是原本的没有补零矩阵data_im中的行列号）
                col2im_add_pixel(data_im, height, width, channels,
                        im_row, im_col, c_im, pad, val);
            }
        }
    }
}

