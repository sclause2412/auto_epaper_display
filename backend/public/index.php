<?php
/**
 * Simple HTTP router for the EPD2 backend.
 * Handles requests for metadata and binary image endpoints.
 * No framework needed; plain PHP.
 */

// Autoload basic classes if composer not available (fallback)
if (!class_exists('App\ImageProcessor')) {
    require_once __DIR__ . '/../src/ImageProcessor.php';
}

// Load configuration
$config = require __DIR__ . '/../config.php';

// Helper: send JSON response with proper headers
function sendJson($data, int $status = 200): void
{
    http_response_code($status);
    header('Content-Type: application/json');
    echo json_encode($data, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
    exit;
}

function requireApiKey(array $config): void
{
    $expected = $config['api_key'] ?? '';
    $provided = $_SERVER['HTTP_X_API_KEY'] ?? $_GET['API_KEY'] ?? '';
    if (
        !is_string($expected)
        || $expected === ''
        || !is_string($provided)
        || !hash_equals($expected, $provided)
    ) {
        sendJson(['error' => 'Forbidden'], 403);
    }
}

function drawCenteredText(\GdImage $image, string $text, int $color, int $scale, int $centerY): void
{
    $font = 5;
    $textWidth = imagefontwidth($font) * strlen($text);
    $textHeight = imagefontheight($font);
    $textImage = imagecreatetruecolor($textWidth, $textHeight);
    $white = imagecolorallocate($textImage, 255, 255, 255);
    imagefill($textImage, 0, 0, $white);
    imagestring($textImage, $font, 0, 0, $text, $color);

    $width = $textWidth * $scale;
    $height = $textHeight * $scale;
    imagecopyresized(
        $image,
        $textImage,
        intdiv(800 - $width, 2),
        $centerY - intdiv($height, 2),
        0,
        0,
        $width,
        $height,
        $textWidth,
        $textHeight
    );
    unset($textImage);
}

function createExampleImage(string $path, string $weekday, string $date): void
{
    $image = imagecreatetruecolor(800, 480);
    $white = imagecolorallocate($image, 255, 255, 255);
    $red = imagecolorallocate($image, 255, 0, 0);
    $black = imagecolorallocate($image, 0, 0, 0);
    imagefill($image, 0, 0, $white);
    drawCenteredText($image, $weekday, $red, 5, 190);
    drawCenteredText($image, $date, $black, 4, 300);
    if (!imagepng($image, $path)) {
        throw new \RuntimeException('Could not write example PNG');
    }
}

function ensureExampleImage(\App\ImageProcessor $processor, array $config): string
{
    $cacheDirectory = $config['cache_dir'];
    if (!is_dir($cacheDirectory) && !mkdir($cacheDirectory, 0777, true) && !is_dir($cacheDirectory)) {
        throw new \RuntimeException('Could not create image cache directory');
    }

    $pngPath = $cacheDirectory . '/example.png';
    $epdPath = $cacheDirectory . '/example.epd';
    $datePath = $cacheDirectory . '/example.date';
    $today = date('Y-m-d');

    if (
        is_file($pngPath)
        && is_file($epdPath)
        && filesize($epdPath) === \App\ImageProcessor::TOTAL_SIZE
        && is_file($datePath)
        && trim((string) file_get_contents($datePath)) === $today
    ) {
        return $epdPath;
    }

    $suffix = bin2hex(random_bytes(6));
    $temporaryPng = $cacheDirectory . '/example-' . $suffix . '.png';
    $temporaryEpd = $cacheDirectory . '/example-' . $suffix . '.epd';
    $temporaryDate = $cacheDirectory . '/example-' . $suffix . '.date';

    try {
        createExampleImage($temporaryPng, date('l'), date('d.m.Y'));
        $processor->process($temporaryPng, $temporaryEpd);
        if (filesize($temporaryEpd) !== \App\ImageProcessor::TOTAL_SIZE) {
            throw new \RuntimeException('Generated EPD file has an invalid size');
        }
        if (file_put_contents($temporaryDate, $today . PHP_EOL) === false) {
            throw new \RuntimeException('Could not write image cache date');
        }

        if (!rename($temporaryPng, $pngPath) || !rename($temporaryEpd, $epdPath) || !rename($temporaryDate, $datePath)) {
            throw new \RuntimeException('Could not update image cache files');
        }
    } finally {
        foreach ([$temporaryPng, $temporaryEpd, $temporaryDate] as $temporaryPath) {
            if (is_file($temporaryPath)) {
                unlink($temporaryPath);
            }
        }
    }

    return $epdPath;
}

function openCurrentImage(\App\ImageProcessor $processor, array $config): array
{
    $path = ensureExampleImage($processor, $config);
    $stream = fopen($path, 'rb');
    if ($stream === false) {
        throw new \RuntimeException('Could not open generated EPD file');
    }

    $stat = fstat($stream);
    if ($stat === false || $stat['size'] !== \App\ImageProcessor::TOTAL_SIZE) {
        fclose($stream);
        throw new \RuntimeException('Generated EPD file has an invalid size');
    }

    $context = hash_init('sha256');
    if (hash_update_stream($context, $stream) === false || !rewind($stream)) {
        fclose($stream);
        throw new \RuntimeException('Could not read generated EPD file');
    }

    return ['stream' => $stream, 'size' => $stat['size'], 'hash' => hash_final($context)];
}

$processor = new \App\ImageProcessor();

// Routing
$requestUri = $_SERVER['REQUEST_URI'] ?? '';
$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';

// Normalize URI: strip query string
$path = parse_url($requestUri, PHP_URL_PATH);
if ($path === false || $path === null) {
    $path = '/';
}

// Route definitions
switch ($method) {
    case 'GET':
        if ($path === '/api/display/current') {
            requireApiKey($config);
            $image = openCurrentImage($processor, $config);
            fclose($image['stream']);
            sendJson([
                'hash' => $image['hash'],
                'width' => \App\ImageProcessor::WIDTH,
                'height' => \App\ImageProcessor::HEIGHT,
                'format' => 'EPD2',
                'size' => $image['size'],
                'url' => '/api/display/image',
            ]);
        }

        if ($path === '/api/display/image') {
            requireApiKey($config);
            $image = openCurrentImage($processor, $config);
            $etag = '"' . $image['hash'] . '"';
            $matches = false;
            foreach (explode(',', $_SERVER['HTTP_IF_NONE_MATCH'] ?? '') as $candidate) {
                $candidate = trim($candidate);
                if ($candidate === '*' || $candidate === $etag || $candidate === $image['hash']) {
                    $matches = true;
                    break;
                }
            }

            header('ETag: ' . $etag);
            if ($matches) {
                fclose($image['stream']);
                http_response_code(304);
                exit;
            }

            header('Content-Type: application/octet-stream');
            header('Content-Length: ' . $image['size']);
            fpassthru($image['stream']);
            fclose($image['stream']);
            exit;
        }

        if ($path === '/') {
            // Simple index page
            header('Content-Type: text/html');
            echo '<!DOCTYPE html><html><body><h1>EPD2 Backend</h1><p>Proof-of-concept backend for battery-powered e-paper picture frame.</p><p>Endpoints: <code>/api/display/current</code>, <code>/api/display/image</code></p></body></html>';
            exit;
        }

        break;

    default:
        http_response_code(405);
        header('Allow: GET');
        header('Content-Type: application/json');
        echo json_encode(['error' => 'Method not allowed']);
        exit;
}

// Default: 404
http_response_code(404);
header('Content-Type: application/json');
echo json_encode(['error' => 'Not found']);
exit;
