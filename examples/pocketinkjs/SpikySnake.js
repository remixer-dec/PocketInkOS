// Copy this file to the SD card and open it from Files.
// Swipe for an absolute direction. MENU turns left; POWER turns right.

const CELL = 10
const COLS = 18
const ROWS = 15
const BOARD_X = 10
const BOARD_Y = 27
const MAX_SNAKE = COLS * ROWS
const MOVE_MS = 500
const SWIPE_MIN = 14

// Named scalar constants are the lightest enum-like representation in PocketInkJS.
const DIR_UP = 0
const DIR_RIGHT = 1
const DIR_DOWN = 2
const DIR_LEFT = 3
const CELL_SNAKE = 1
const CELL_SPIKE = 2

const directionX = [0, 1, 0, -1]
const directionY = [-1, 0, 1, 0]

// These fixed-size arrays form a circular queue from tail to head.
const bodyX = new Array(MAX_SNAKE).fill(0)
const bodyY = new Array(MAX_SNAKE).fill(0)

const spikeX = [2, 8, 14, 5, 12, 16, 2, 9, 15, 5, 12, 1]
const spikeY = [2, 2, 3, 5, 6, 5, 9, 9, 10, 12, 13, 14]
const cells = new Array(MAX_SNAKE).fill(0)
for (let i = 0; i < spikeX.length; i += 1) {
  cells[spikeY[i] * COLS + spikeX[i]] = CELL_SPIKE
}

let tailIndex = 0
let headIndex = 0
let snakeLength = 0
let direction = DIR_RIGHT
let nextDirection = DIR_RIGHT
let foodX = 0
let foodY = 0
let foodEaten = 0
let spikeHits = 0
let moveTimer = 0
let swipeStartX = 0
let swipeStartY = 0
let trackingSwipe = false
let gameOver = false

function isSpikeAt(x, y) {
  return (cells[y * COLS + x] & CELL_SPIKE) !== 0
}

function isSnakeAt(x, y, skipTail) {
  if ((cells[y * COLS + x] & CELL_SNAKE) === 0) return false
  return !(skipTail && bodyX[tailIndex] === x && bodyY[tailIndex] === y)
}

function rebuildOccupancy() {
  for (let i = 0; i < MAX_SNAKE; i += 1) {
    cells[i] = cells[i] & CELL_SPIKE
  }
  for (let i = 0; i < snakeLength; i += 1) {
    const index = (tailIndex + i) % MAX_SNAKE
    const cell = bodyY[index] * COLS + bodyX[index]
    cells[cell] = cells[cell] | CELL_SNAKE
  }
}

function placeFood() {
  // A random starting point plus a full scan is bounded and always terminates.
  const start = Math.floor(Math.random() * MAX_SNAKE)
  for (let offset = 0; offset < MAX_SNAKE; offset += 1) {
    const cell = (start + offset) % MAX_SNAKE
    const x = cell % COLS
    const y = Math.floor(cell / COLS)
    if (cells[cell] === 0) {
      foodX = x
      foodY = y
      return true
    }
  }
  gameOver = true
  return false
}

function resetGame() {
  tailIndex = 0
  snakeLength = 4
  headIndex = snakeLength - 1
  direction = DIR_RIGHT
  nextDirection = DIR_RIGHT
  foodEaten = 0
  spikeHits = 0
  gameOver = false

  const y = Math.floor(ROWS / 2)
  for (let i = 0; i < snakeLength; i += 1) {
    bodyX[i] = 6 + i
    bodyY[i] = y
  }
  rebuildOccupancy()
  placeFood()
  console.log("[snake] reset food", foodX, foodY)
  return true
}

function oppositeDirection(value) {
  return (value + 2) % 4
}

function setDirection(value) {
  if (gameOver) return resetGame()
  // Accept only one turn per movement tick and never reverse into the neck.
  if (nextDirection !== direction) return false
  if (snakeLength > 1 && value === oppositeDirection(direction)) return false
  nextDirection = value
  return true
}

function turnLeft() {
  return setDirection((direction + 3) % 4)
}

function turnRight() {
  return setDirection((direction + 1) % 4)
}

function stepGame() {
  if (gameOver) return false
  direction = nextDirection

  let x = bodyX[headIndex] + directionX[direction]
  let y = bodyY[headIndex] + directionY[direction]
  if (x < 0) x = COLS - 1
  if (x >= COLS) x = 0
  if (y < 0) y = ROWS - 1
  if (y >= ROWS) y = 0

  const ateFood = x === foodX && y === foodY
  if (isSnakeAt(x, y, !ateFood)) {
    gameOver = true
    console.log("[snake] game over score/length", foodEaten, snakeLength)
    return true
  }

  const nextCell = y * COLS + x
  const tailCell = bodyY[tailIndex] * COLS + bodyX[tailIndex]
  headIndex = (headIndex + 1) % MAX_SNAKE
  bodyX[headIndex] = x
  bodyY[headIndex] = y

  if (ateFood && snakeLength < MAX_SNAKE) {
    snakeLength += 1
    foodEaten += 1
    console.log("[snake] food score/length", foodEaten, snakeLength)
    placeFood()
  } else {
    cells[tailCell] = cells[tailCell] & CELL_SPIKE
    tailIndex = (tailIndex + 1) % MAX_SNAKE
  }
  cells[nextCell] = cells[nextCell] | CELL_SNAKE

  if (isSpikeAt(x, y) && snakeLength > 1) {
    const lostCell = bodyY[tailIndex] * COLS + bodyX[tailIndex]
    cells[lostCell] = cells[lostCell] & CELL_SPIKE
    tailIndex = (tailIndex + 1) % MAX_SNAKE
    snakeLength -= 1
    spikeHits += 1
    console.log("[snake] spike hits/length", spikeHits, snakeLength)
  }
  return true
}

function drawSpike(x, y) {
  const left = BOARD_X + x * CELL
  const top = BOARD_Y + y * CELL
  ui.drawLine(left + 2, top + 2, left + 7, top + 7, ui.BLACK)
  ui.drawLine(left + 7, top + 2, left + 2, top + 7, ui.BLACK)
  ui.drawPixel(left + 4, top + 1, ui.BLACK)
  ui.drawPixel(left + 4, top + 8, ui.BLACK)
}

function drawFood() {
  const left = BOARD_X + foodX * CELL
  const top = BOARD_Y + foodY * CELL
  ui.drawRect(left + 2, top + 2, 6, 6, ui.BLACK)
  ui.fillRect(left + 4, top + 4, 2, 2, ui.BLACK)
}

function drawSnake() {
  for (let i = 0; i < snakeLength; i += 1) {
    const index = (tailIndex + i) % MAX_SNAKE
    const left = BOARD_X + bodyX[index] * CELL
    const top = BOARD_Y + bodyY[index] * CELL
    if (index === headIndex) {
      ui.fillRect(left + 1, top + 1, 8, 8, ui.BLACK)
      ui.drawPixel(left + 3, top + 3, ui.WHITE)
      ui.drawPixel(left + 6, top + 3, ui.WHITE)
    } else {
      ui.fillRect(left + 2, top + 2, 6, 6, ui.BLACK)
    }
  }
}

function onStart() {
  console.log("[snake] start", app.path)
  app.setWakeLock(true)
  resetGame()
  moveTimer = setInterval(stepGame, MOVE_MS)
}

function onStop() {
  clearInterval(moveTimer)
  app.setWakeLock(false)
  console.log("[snake] stop score/hits/length", foodEaten, spikeHits,
              snakeLength)
}

function onDraw() {
  ui.fillScreen(ui.WHITE)
  ui.setTextColor(ui.BLACK)
  ui.setTextSize(1)
  ui.setCursor(10, 14)
  ui.print("SPIKY SNAKE")
  ui.setCursor(92, 14)
  ui.print(`FOOD ${foodEaten}  LEN ${snakeLength}`)

  ui.drawRect(BOARD_X - 1, BOARD_Y - 1, COLS * CELL + 2,
              ROWS * CELL + 2, ui.BLACK)
  for (let i = 0; i < spikeX.length; i += 1) drawSpike(spikeX[i], spikeY[i])
  drawFood()
  drawSnake()

  ui.setCursor(26, 193)
  ui.print("SWIPE | MENU <  > POWER")
  if (gameOver) {
    ui.fillRect(43, 82, 114, 48, ui.WHITE)
    ui.drawRect(43, 82, 114, 48, ui.BLACK)
    ui.setTextSize(2)
    ui.setCursor(54, 101)
    ui.print("GAME OVER")
    ui.setTextSize(1)
    ui.setCursor(58, 120)
    ui.print("press or swipe")
  }
}

function onTouchDown(event) {
  swipeStartX = event.x
  swipeStartY = event.y
  trackingSwipe = true
  return false
}

function onTouchUp(event) {
  if (!trackingSwipe) return false
  trackingSwipe = false
  if (gameOver) return resetGame()

  const dx = event.x - swipeStartX
  const dy = event.y - swipeStartY
  if (Math.abs(dx) < SWIPE_MIN && Math.abs(dy) < SWIPE_MIN) return false
  if (Math.abs(dx) > Math.abs(dy)) {
    return setDirection(dx > 0 ? DIR_RIGHT : DIR_LEFT)
  }
  return setDirection(dy > 0 ? DIR_DOWN : DIR_UP)
}

function onMenu() {
  return turnLeft()
}

function onPower() {
  return turnRight()
}
