#include <math.h>
#include <QDebug>
#include "colorblob.h"
#include "monmodule.h"

ColorBlob::ColorBlob(uint8_t *lut)
{
    m_tol = DEFAULT_TOL;
    m_lut = lut;
    m_minSat = 2.0;
    m_miny = DEFAULT_MINY;
    m_acqRange = DEFAULT_RANGE;
    m_trackRange = 1.0;
    clearLUT();
}

ColorBlob::~ColorBlob()
{
}


// lt==true means number of pixels less than line
float ColorBlob::calcRatio(const int32_t *uvPixels, uint32_t numuv, int32_t line, bool lt)
{
    uint32_t i;
    uint32_t count;

    for (i=0, count=0; i<numuv; i++)
    {
        if ((lt && uvPixels[i]<line) || (!lt && uvPixels[i]>line))
            count++;
    }

    return (float)count/numuv;
}

// This is a binary search --- it's guaranteed get to within +/-1 of the optimal value, which is good enough!
int32_t ColorBlob::iterate(const int32_t *uvPixels, uint32_t numuv, float ratio, bool pos)
{
    int32_t scale, line;
    float ri;

    //qDebug()<< "begin";
    for (scale=1<<30, line=0; scale!=0; scale>>=1)
    {
        ri = calcRatio(uvPixels, numuv, line, pos);
        if (pos)
        {
            if (ri>ratio)
                line -=scale;
            else
                line += scale;
        }
        else
        {
            if (ri>ratio)
                line +=scale;
            else
                line -= scale;
        }
        //qDebug("%f %d", ri, line);
    }

    //qDebug()<< "end";
    return line;
}

int ColorBlob::generateSignature(const Frame8 &frame, const RectA &region, ColorSignature *signature)
{
    int32_t x, y, r, g1, g2, b, count, u, v, c;
    uint8_t *pixels;
    int32_t numuv = (region.m_width/2 + 1)*(region.m_height/2 + 1);
    int32_t *uPixels = new int32_t[numuv];
    int32_t *vPixels = new int32_t[numuv];

    pixels = frame.m_pixels + (region.m_yOffset | 1)*frame.m_width + (region.m_xOffset | 1);
    for (y=0, count=0; y<region.m_height && count<numuv; y+=2, pixels+=frame.m_width*2)
    {
        for (x=0; x<region.m_width && count<numuv; x+=2, count++)
        {
            r = pixels[x];
            g1 = pixels[x - 1];
            g2 = pixels[-frame.m_width + x];
            b = pixels[-frame.m_width + x - 1];
            c = r+g1+b;
            if (c==0)
                c = 1;
            u = ((r-g1)<<LUT_ENTRY_SCALE)/c;
            c = r+g2+b;
            if (c==0)
                c = 1;
            v = ((b-g2)<<LUT_ENTRY_SCALE)/c;
            uPixels[count] = u;
            vPixels[count] = v;
        }
    }

    signature->m_uMin = iterate(uPixels, count, m_tol, false);
    signature->m_uMax = iterate(uPixels, count, m_tol, true);
    signature->m_vMin = iterate(vPixels, count, m_tol, false);
    signature->m_vMax = iterate(vPixels, count, m_tol, true);

    delete [] uPixels;
    delete [] vPixels;

    // if signs of u's and v's are *both* different, our envelope is greater than 90 degrees and it's an indication
    // that we don't have much of a lock
    if ((signature->m_uMin>0)!=(signature->m_uMax>0) && (signature->m_vMin>0)!=(signature->m_vMax>0))
        cprintf("Warning: signature may be poorly defined.");

    return 0;
}

int ColorBlob::generateSignature(const Frame8 &frame, const Point16 &point, Points *points, ColorSignature *signature)
{
    growRegion(frame, point, points);
    int32_t i, x, y, r, g1, g2, b, count, u, v, c;
    uint8_t *pixels;
    int32_t numuv = points->size()*GROW_INC*GROW_INC/4;
    int32_t *uPixels = new int32_t[numuv];
    int32_t *vPixels = new int32_t[numuv];

    for (i=0, count=0; i<points->size(); i++)
    {
        pixels = frame.m_pixels + ((*points)[i].m_y | 1)*frame.m_width + ((*points)[i].m_x | 1);
        for (y=0; y<GROW_INC && count<numuv; y+=2, pixels+=frame.m_width*2)
        {
            for (x=0; x<GROW_INC && count<numuv; x+=2, count++)
            {
                r = pixels[x];
                g1 = pixels[x - 1];
                g2 = pixels[-frame.m_width + x];
                b = pixels[-frame.m_width + x - 1];
                c = r+g1+b;
                if (c==0)
                    c = 1;
                u = ((r-g1)<<LUT_ENTRY_SCALE)/c;
                c = r+g2+b;
                if (c==0)
                    c = 1;
                v = ((b-g2)<<LUT_ENTRY_SCALE)/c;
                uPixels[count] = u;
                vPixels[count] = v;
            }
        }
    }

    signature->m_uMin = iterate(uPixels, count, m_tol, false);
    signature->m_uMax = iterate(uPixels, count, m_tol, true);
    signature->m_vMin = iterate(vPixels, count, m_tol, false);
    signature->m_vMax = iterate(vPixels, count, m_tol, true);

    delete [] uPixels;
    delete [] vPixels;

    // if signs of u's and v's are *both* different, our envelope is greater than 90 degrees and it's an indication
    // that we don't have much of a lock
    if ((signature->m_uMin>0)!=(signature->m_uMax>0) && (signature->m_vMin>0)!=(signature->m_vMax>0))
        cprintf("Warning: signature may be poorly defined.");

    return 0;
}

#if 0
int ColorBlob::generateLUT(const ColorSignature *signature, uint8_t signum)
{
    int32_t u, v, i, j, bin, signChange=0;
    bool u0, v0, lutVal;
    float bratios[4], c, umin, umax, vmin, vmax, ratio, sat, minRatio, maxRatio;

    clearLUT(signum);

    // scale up
    c = ((float)signature->m_uMax + signature->m_uMin)/2.0f;
    umin = c + (signature->m_uMin - c)*m_acqRange*m_trackRange;
    umax = c + (signature->m_uMax - c)*m_acqRange*m_trackRange;
    c = ((float)signature->m_vMax + signature->m_vMin)/2.0f;
    vmin = c + (signature->m_vMin - c)*m_acqRange*m_trackRange;
    vmax = c + (signature->m_vMax - c)*m_acqRange*m_trackRange;

    // count the sign changes
    if ((umin>0.0f)!=(signature->m_uMin>0))
        signChange++;
    if ((umax>0.0f)!=(signature->m_uMax>0))
        signChange++;
    if ((vmin>0.0f)!=(signature->m_vMin>0))
        signChange++;
    if ((vmax>0.0f)!=(signature->m_vMax>0))
        signChange++;

    // if we've changed signs more than once, we've overflowed
    if (signChange>1)
        return -1; // overflow

    // calculate ratios
    bratios[0] = vmin/umin;
    bratios[1] = vmin/umax;
    bratios[2] = vmax/umin;
    bratios[3] = vmax/umax;

    // find max/min ratio values
    if ((umin>0.0f)==(umax>0.0f))
    {
        // find normal max and min
        for (i=0, maxRatio=-10000.0f, minRatio=10000.0f; i<4; i++)
        {
            if (bratios[i]>maxRatio)
                maxRatio = bratios[i];
            if (bratios[i]<minRatio)
                minRatio = bratios[i];
        }
    }
    else // special case where lines straddle the y axis
    {
        // find smallest positive and largest negative
        for (i=0, maxRatio=10000.0f, minRatio=-10000.0f; i<4; i++)
        {
            if (bratios[i]>0 && bratios[i]<maxRatio) // positive and less than
                maxRatio = bratios[i];
            if (bratios[i]<0 && bratios[i]>minRatio) // negative and greater than
                minRatio = bratios[i];
        }
    }

    for (i=0, bin=0; i<(1<<LUT_COMPONENT_SCALE); i++)
    {
        for (j=0; j<(1<<LUT_COMPONENT_SCALE); j++, bin++)
        {
            u = (i<<(32-LUT_COMPONENT_SCALE))>>(32-LUT_COMPONENT_SCALE); // sign extend and shift right/divide
            v = (j<<(32-LUT_COMPONENT_SCALE))>>(32-LUT_COMPONENT_SCALE); // sign extend and shift right/divide

            u0 = u>0;
            v0 = v>0;
            ratio = (float)v/u;
            sat = sqrt((float)(u*u) + (float)(v*v));

            if (sat<m_minSat)
                continue;

            // knock off upper bits
            u &= (1<<LUT_COMPONENT_SCALE)-1;
            v &= (1<<LUT_COMPONENT_SCALE)-1;

            lutVal = false;
            if ((umin>0.0f)==(umax>0.0f)) // left or right half of hue plane
            {
                if (u0==(umin>0.0f) && minRatio<ratio && ratio<maxRatio) // make sure sign of u is the same is umin
                    lutVal = true;
            }
            else if (v0==(vmin>0.0f)) // make sure signe of v is the same as vmin  top or bottom
            {
                if (maxRatio<ratio || ratio<minRatio) // in this region, the ratio signs change based on which half of the hue plane you're on
                    lutVal = true;
            }

            if (lutVal && (m_lut[bin]==0 || m_lut[bin]>signum))
                m_lut[bin] = signum;

        }
    }
    return 0;
}
#else
int ColorBlob::generateLUT(const RuntimeSignature signatures[])
{
    int32_t r, g, b, u, v, y, bin, miny, sig;

    clearLUT();

    miny = (3*(1<<8)-1)*m_miny;
    if (miny==0)
        miny = 1;

    for (r=0; r<1<<8; r+=1<<(8-LUT_COMPONENT_SCALE))
    {
        for (g=0; g<1<<8; g+=1<<(8-LUT_COMPONENT_SCALE))
        {
            for (b=0; b<1<<8; b+=1<<(8-LUT_COMPONENT_SCALE))
            {
                y = r+g+b;

                if (y<miny)
                    continue;
                u = ((r-g)<<LUT_ENTRY_SCALE)/y;
                v = ((b-g)<<LUT_ENTRY_SCALE)/y;

                for (sig=0; sig<NUM_SIGNATURES; sig++)
                {
                    if (signatures[sig].m_uMin==0 && signatures[sig].m_uMax==0)
                        continue;
                    if ((signatures[sig].m_uMin<u) && (u<signatures[sig].m_uMax) &&
                            (signatures[sig].m_vMin<v) && (v<signatures[sig].m_vMax))
                    {
                        u = r-g;
                        u >>= 9-LUT_COMPONENT_SCALE;
                        u &= (1<<LUT_COMPONENT_SCALE)-1;
                        v = b-g;
                        v >>= 9-LUT_COMPONENT_SCALE;
                        v &= (1<<LUT_COMPONENT_SCALE)-1;

                        bin = (u<<LUT_COMPONENT_SCALE)+ v;

                        if (m_lut[bin]==0)
                            m_lut[bin] = sig+1;
                    }
                }
            }
        }
    }
    return 0;
}
#endif

void ColorBlob::clearLUT(uint8_t signum)
{
    int i;

    for (i=0; i<LUT_SIZE; i++)
    {
        if (signum==0)
            m_lut[i] = 0;
        else if (m_lut[i]==signum)
            m_lut[i] = 0;
    }
}


bool ColorBlob::growRegion(RectA *region, const Frame8 &frame, uint8_t dir)
{
    if (dir==0) // grow left
    {
        if (region->m_xOffset>=GROW_INC)
        {
            region->m_xOffset -= GROW_INC;
            region->m_width += GROW_INC;
        }
        else
            return true;
    }
    else if (dir==1) // grow top
    {
        if (region->m_yOffset>=GROW_INC)
        {
            region->m_yOffset -= GROW_INC;
            region->m_height += GROW_INC;
        }
        else
            return true;
    }
    else if (dir==2) // grow right
    {
        if (region->m_xOffset+region->m_width+GROW_INC>frame.m_width)
            return true;
        region->m_width += GROW_INC;
    }
    else if (dir==3) // grow bottom
    {
        if (region->m_yOffset+region->m_height+GROW_INC>frame.m_height)
            return true;
        region->m_height += GROW_INC;
    }
    return false;
}

float ColorBlob::testRegion(const RectA &region, const Frame8 &frame, Point32 *mean, Points *points)
{
    Point32 subMean;
    float distance;
    RectA subRegion(0, 0, GROW_INC, GROW_INC);
    subRegion.m_xOffset = region.m_xOffset;
    subRegion.m_yOffset = region.m_yOffset;
    bool horiz = region.m_width>region.m_height;
    uint32_t i, test, endpoint = horiz ? region.m_width : region.m_height;

    for (i=0, test=0; i<endpoint; i+=GROW_INC)
    {
        getMean(subRegion, frame, &subMean);
        distance = sqrt((mean->m_x-subMean.m_x)*(mean->m_x-subMean.m_x) + (mean->m_y-subMean.m_y)*(mean->m_y-subMean.m_y));
        if ((uint32_t)distance<m_maxDist)
        {
            int32_t n = points->size();
            mean->m_x = ((qlonglong)mean->m_x*n + subMean.m_x)/(n+1);
            mean->m_y = ((qlonglong)mean->m_y*n + subMean.m_y)/(n+1);
            points->push_back(Point16(subRegion.m_xOffset, subRegion.m_yOffset));
            qDebug("add %d %d %d", subRegion.m_xOffset, subRegion.m_yOffset, points->size());
            test++;
        }

        if (horiz)
            subRegion.m_xOffset += GROW_INC;
        else
            subRegion.m_yOffset += GROW_INC;
    }

    qDebug("return %f", (float)test*GROW_INC/endpoint);
    return (float)test*GROW_INC/endpoint;
}


void ColorBlob::getMean(const RectA &region ,const Frame8 &frame, Point32 *mean)
{
    uint32_t x, y, count;
    int32_t r, g1, g2, b, u, v, c, miny;
    uint8_t *pixels;
    qlonglong usum, vsum;

    pixels = frame.m_pixels + (region.m_yOffset | 1)*frame.m_width + (region.m_xOffset | 1);
    miny = (3*(1<<8)-1)*0.05;
    for (y=0, count=0, usum=0, vsum=0; y<region.m_height; y+=2, pixels+=frame.m_width*2)
    {
        for (x=0; x<region.m_width; x+=2, count++)
        {
            r = pixels[x];
            g1 = pixels[x - 1];
            g2 = pixels[-frame.m_width + x];
            b = pixels[-frame.m_width + x - 1];
            c = r+g1+b;
            if (c<miny)
                continue;
            u = ((r-g1)<<LUT_ENTRY_SCALE)/c;
            c = r+g2+b;
            if (c<miny)
                continue;
            v = ((b-g2)<<LUT_ENTRY_SCALE)/c;
            usum += u;
            vsum += v;
        }
    }
    mean->m_x = usum/count;
    mean->m_y = vsum/count;
}

void ColorBlob::growRegion(const Frame8 &frame, const Point16 &seed, Points *points)
{
    uint8_t dir, done;
    RectA region, newRegion;
    Point32 mean;
    float ratio;

    done = 0;

    // create seed 2*GROW_INCx2*GROW_INC region from seed position, make sure it's within the frame
    region.m_xOffset = seed.m_x;
    region.m_yOffset = seed.m_y;
    if (growRegion(&region, frame, 0))
        done |= 1<<0;
    else
        points->push_back(Point16(region.m_xOffset, region.m_yOffset));
    if (growRegion(&region, frame, 1))
        done |= 1<<1;
    else
        points->push_back(Point16(region.m_xOffset, region.m_yOffset));
    if (growRegion(&region, frame, 2))
        done |= 1<<2;
    else
        points->push_back(Point16(seed.m_x, region.m_yOffset));
    if (growRegion(&region, frame, 3))
        done |= 1<<3;
    else
        points->push_back(seed);

    getMean(region, frame, &mean);

    while(done!=0x0f)
    {
        for (dir=0; dir<4; dir++)
        {
            newRegion = region;
            if (done&(1<<dir))
                continue;
            else if (dir==0) // left
                newRegion.m_width = 0;
            else if (dir==1) // top
                newRegion.m_height = 0; // top and bottom
            else if (dir==2) // right
            {
                newRegion.m_xOffset += newRegion.m_width;
                newRegion.m_width = 0;
            }
            else if (dir==3) // bottom
            {
                newRegion.m_yOffset += newRegion.m_height;
                newRegion.m_height = 0;
            }

            if (growRegion(&newRegion, frame, dir))
                done |= 1<<dir;
            else
            {
                ratio = testRegion(newRegion, frame, &mean, points);
                if (ratio<m_minRatio)
                    done |= 1<<dir;
                else
                    growRegion(&region, frame, dir);
            }
        }
    }
}


void ColorBlob::setParameters(float range, float miny, uint32_t maxDist, float minRatio)
{
    m_acqRange = range;
    m_miny = miny;
    m_maxDist = maxDist;
    m_minRatio = minRatio;
}


