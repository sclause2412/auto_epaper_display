<?php
namespace App;

class ImageProcessor
{
    private const HEADER_SIZE = 5;

    public static function planeSize(int $width, int $height): int
    {
        return intdiv($width * $height + 7, 8);
    }

    public static function totalSize(int $width, int $height, int $colorCount): int
    {
        return self::HEADER_SIZE + self::planeSize($width, $height) * $colorCount;
    }

    public function process(
        string $pngPath,
        string $epdPath,
        int $width,
        int $height,
        array $colors
    ): void {
        $image = @imagecreatefrompng($pngPath);
        if ($image === false) {
            throw new \RuntimeException('Could not read generated PNG');
        }
        if (imagesx($image) !== $width || imagesy($image) !== $height) {
            throw new \RuntimeException('Generated PNG has the wrong dimensions');
        }
        if ($width < 1 || $height < 1 || $width > 65535 || $height > 65535) {
            throw new \RuntimeException('Invalid EPD2 dimensions');
        }
        if (count($colors) < 1 || count($colors) > 255) {
            throw new \RuntimeException('Invalid EPD2 color count');
        }

        $planeSize = self::planeSize($width, $height);
        $planes = [];
        foreach ($colors as $color) {
            $planes[$color] = str_repeat("\0", $planeSize);
        }

        for ($y = 0; $y < $height; $y++) {
            for ($x = 0; $x < $width; $x++) {
                $rgb = imagecolorat($image, $x, $y);
                $r = ($rgb >> 16) & 0xff;
                $g = ($rgb >> 8) & 0xff;
                $b = $rgb & 0xff;

                $matchedColor = $this->nearestColor($r, $g, $b);
                if ($matchedColor === 'white' || !isset($planes[$matchedColor])) {
                    continue;
                }

                $pixelIndex = $y * $width + $x;
                $byte = intdiv($pixelIndex, 8);
                $bit = 1 << ($pixelIndex % 8);
                $planes[$matchedColor][$byte] = chr(ord($planes[$matchedColor][$byte]) | $bit);
            }
        }

        $header = pack('nnC', $width, $height, count($colors));
        $binary = $header . implode('', array_map(
            static fn(string $color): string => $planes[$color],
            $colors
        ));
        $expectedSize = self::totalSize($width, $height, count($colors));
        if (strlen($binary) !== $expectedSize) {
            throw new \RuntimeException('Generated EPD file has an invalid size');
        }
        if (file_put_contents($epdPath, $binary) !== $expectedSize) {
            throw new \RuntimeException('Could not write generated EPD file');
        }
    }

    private function nearestColor(int $r, int $g, int $b): string
    {
        $palette = [
            'white' => [255, 255, 255],
            'black' => [0, 0, 0],
            'red' => [255, 0, 0],
            'yellow' => [255, 255, 0],
        ];
        $best = 'white';
        $bestDistance = PHP_INT_MAX;
        foreach ($palette as $name => [$pr, $pg, $pb]) {
            $distance = ($r - $pr) ** 2 + ($g - $pg) ** 2 + ($b - $pb) ** 2;
            if ($distance < $bestDistance) {
                $bestDistance = $distance;
                $best = $name;
            }
        }
        return $best;
    }
}
