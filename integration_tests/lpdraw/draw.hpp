#include <iostream>
#include <xtensor/xtensor.hpp>

void Pixel(int32_t H, int32_t W, xt::xtensor<int32_t, 2>& Screen, int32_t x, int32_t y) {
    if( x >= 0 and y >= 0 and x < W and y < H ) {
        Screen(H - 1 - y, x) = 255;
    }
}

void Clear(int32_t H, int32_t W, xt::xtensor<int32_t, 2>& Screen) {
    int32_t i;
    int32_t j;
    for( i = 0; i < H; i++ ) {
        for( j = 0; j < W; j++ ) {
            Screen(i, j) = 0;
        }
    }
}

void DisplayTerminal(int32_t H, int32_t W, xt::xtensor<int32_t, 2>& Screen) {
    int32_t i, j;

    std::cout<<"+";
    for( i = 0; i < W; i++ ) {
        std::cout<<"-";
    }
    std::cout<<"+\n";

    for( i = 0; i < H; i++ ) {
        std::cout<<"|";
        for( j = 0; j < W; j++ ) {
            if( bool(Screen(i, j)) ) {
                std::cout<<".";
            } else {
                std::cout<<" ";
            }
        }
        std::cout<<"|";
    }

    std::cout<<"+";
    for( i = 0; i < W; i++ ) {
        std::cout<<"-";
    }
    std::cout<<"+";
}

void Display(int32_t H, int32_t W, xt::xtensor<int32_t, 2>& Screen) {
    int32_t i, j;

    std::cout<<"P2";
    std::cout<<W<<" "<<H;
    std::cout<<255;

    for( i = 0; i < H; i++ ) {
        for( j = 0; j < W; j++ ) {
            std::cout<<Screen(i, j);
        }
    }
}

void Line(int32_t H, int32_t W, xt::xtensor<int32_t, 2>& Screen, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dx = abs(x2 - x1);
    int32_t dy = abs(y2 - y1);

    int32_t sx, sy;

    if( x1 < x2 ) {
        sx = 1;
    } else {
        sx = -1;
    }

    if( y1 < y2 ) {
        sy = 1;
    } else {
        sy = -1;
    }

    int32_t err = dx - dy;

    while( x1 != x2 or y1 != y2 ) {
        Pixel(H, W, Screen, x1, y1);
        int32_t e2 = 2 * err;

        if( e2 > -dy ) {
            err -= dy;
            x1 += sx;
        }

        if( x1 == x2 and y1 == y2 ) {
            Pixel(H, W, Screen, x1, y1);
            break;
        }

        if( e2 < dx ) {
            err += dx;
            y1 += sy;
        }
    }
}

void Circle(int32_t H, int32_t W, xt::xtensor<int32_t, 2>& Screen, int32_t x, int32_t y, double r) {
    int32_t x0 = r;
    int32_t y0 = 0;
    int32_t err = 0;

    while( x0 >= y0 ) {
        Pixel(H, W, Screen, x + x0, y + y0);
        Pixel(H, W, Screen, x - x0, y + y0);
        Pixel(H, W, Screen, x + x0, y - y0);
        Pixel(H, W, Screen, x - x0, y - y0);
        Pixel(H, W, Screen, x + y0, y + x0);
        Pixel(H, W, Screen, x - y0, y + x0);
        Pixel(H, W, Screen, x + y0, y - x0);
        Pixel(H, W, Screen, x - y0, y - x0);

        if( err <= 0 ) {
            y0 += 1;
            err += 2 * y0 + 1;
        }
        if( err > 0 ) {
            x0 -= 1;
            err -= 2 * x0 + 1;
        }
    }
}
