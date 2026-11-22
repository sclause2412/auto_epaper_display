<?php
namespace App;

class ImageProcessor
{
    public const WIDTH = 800;
    public const HEIGHT = 480;
    public const PLANE_SIZE = 48000;
    public const TOTAL_SIZE = self::PLANE_SIZE * 2;

    public function process(string $pngPath, string $epdPath): void
    {
        $image = @imagecreatefrompng($pngPath);
        if ($image === false) {
            throw new \RuntimeException('Could not read generated PNG');
        }
        if (imagesx($image) !== self::WIDTH || imagesy($image) !== self::HEIGHT) {
            throw new \RuntimeException('Generated PNG must be 800 x 480 pixels');
        }

        $blackPlane = str_repeat("\0", self::PLANE_SIZE);
        $redPlane = str_repeat("\0", self::PLANE_SIZE);

        for ($y = 0; $y < self::HEIGHT; $y++) {
            for ($x = 0; $x < self::WIDTH; $x++) {
                $rgb = imagecolorat($image, $x, $y);
                $r = ($rgb >> 16) & 0xff;
                $g = ($rgb >> 8) & 0xff;
                $b = $rgb & 0xff;

                $blackDistance = $r * $r + $g * $g + $b * $b;
                $whiteDistance = (255 - $r) ** 2 + (255 - $g) ** 2 + (255 - $b) ** 2;
                $redDistance = (255 - $r) ** 2 + $g * $g + $b * $b;
                $byte = $y * (self::WIDTH >> 3) + ($x >> 3);
                $bit = 1 << ($x % 8);

                if ($blackDistance <= $whiteDistance && $blackDistance <= $redDistance) {
                    $blackPlane[$byte] = chr(ord($blackPlane[$byte]) | $bit);
                } elseif ($redDistance < $whiteDistance) {
                    $redPlane[$byte] = chr(ord($redPlane[$byte]) | $bit);
                }
            }
        }

        $binary = $blackPlane . $redPlane;
        if (strlen($binary) !== self::TOTAL_SIZE) {
            throw new \RuntimeException('Generated EPD file has an invalid size');
        }
        if (file_put_contents($epdPath, $binary) !== self::TOTAL_SIZE) {
            throw new \RuntimeException('Could not write generated EPD file');
        }
    }
}
