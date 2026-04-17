
////////////////////////////////////////////////////////////////////////////////
//Инициализация библиотеки LGBlockNavigation_V2.2///////////////////////////////
////////////////////////////////////////////////////////////////////////////////

var NB = new BlockListClass();

var sound1;
var sound2;
var sound3;

var START_SCREEN = 0;
var GAMEPLAY_SCREEN = 1;
var HELP_SCREEN = 2;
var SCORE_SCREEN = 4;
var PAUSE_SCREEN = 5;

var display = START_SCREEN; // текущий слой
////////////////////////////////////////////////////////////////////////////////
//Игра//////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
//переменные
var canvas, context;
var time = 0;
var sec = min = 0;
var istart = count_collision = 0;
var Gametimer;
var oBall, oPlatform;
var SceneW = 1214;
var SceneH = 624;
var SpeedX; // направление движения шара по Х при старте
var bRightBut = false;
var bLeftBut = false;
var isFire = false;
var isFirstTouch = true;
var isFireConfirm = false;
var Pause = false;
var fps = 20;
var tokens; //перевод

//размеры сцены
var SceneOffsetX = 32;
var SceneOffsetY = 64;
var SceneX1 = SceneOffsetX;
var SceneX2 = SceneX1 + SceneW;
var SceneY1 = SceneOffsetY;
var SceneY2 = SceneY1 + SceneH;

//параметры блоков
var Level = []; //массив с координатами
var Bonuses = []; //массив с координатами бонусов
var blocW = 87;
var blocH = 35;
var blocOffsetX = 25;
var blocOffsetY = 60;
var blocGap = 3;
var typeBloc; //для подсчета очков

//параметры игры
var LevelNum = 0; //первый уровень
var Score = 0; //количество разбитых блоков
var currentScore = 0; //очки
// fix 2018.08.02
var LastScore = 0;
var Life = 3; //жизни
var isMusic = true; //музыка

// Чит-коды
var buttonsStack = [];
var superBallButtonsOrder = [403, 404, 405, 406];
var extraWidePlatformOrder = [406, 405, 404, 403];


function Ball(x, y, dx, dy, r, img, isSuper) {
    this.x = x;
    this.y = y;
    this.dx = dx;
    this.dy = dy;
    this.r = r;
    this.img = img;
    this.isSuper = isSuper;
}

function Platform(x, y, w, h, img, centerW) {
    this.x = x;
    this.y = y;
    this.targetX = x;
    this.w = w;
    this.h = h;
    this.img = img;
    this.centerW = centerW;
}

function Bloc(x, y, w, h, t, id, bonus) {
    this.x = x;
    this.y = y;
    this.w = w;
    this.h = h;
    this.t = t;
    this.id = id;
    this.bonus = bonus;
}

function Bonus(x, y, w, h, t, isMove, id) {
    this.x = x;
    this.y = y;
    this.w = w;
    this.h = h;
    this.t = t;
    this.isMove = isMove;
    this.id = id;
}

//создаем случайный вектор направления по X
function getrandom(min_random, max_random) {
    var range = max_random - min_random + 1;
    SpeedX = Math.floor(Math.random() * range) + min_random;
    if (SpeedX == 0) {
        SpeedX = 2;
    }
}

//создаем массив Level c объектами Bloc и создаем блоки по шаблону
function InitLevel(levl, l) {
    if (levl.length > l) {
        var parent = document.getElementById('scene');
        var k = 0;
        for (i = 0; i < levl[l].length; i++) {
            for (j = 0; j < levl[l][i].length; j++) {
                if (levl[l][i][j] != 0) {
                    //присваиваем ему атрибуты
                    var blocId = k;
                    var $scene = $('#scene');
                    $scene.append('<div id="bloc' + blocId + '"></div>');

                    var $block = $('#bloc' + blocId);
                    $block.css({
                        'position': 'fixed',
                        'width': blocW,
                        'height': blocH
                    });
                    switch (levl[l][i][j]) {                //закрашивает относительно типа блока
                        case 1:
                            $block.css('background', 'url(images/tiles.png) -261px 0 no-repeat');
                            break;
                        case 2:
                            $block.css('background', 'url(images/tiles.png) -174px 0 no-repeat');
                            break;
                        case 3:
                            $block.css('background', 'url(images/tiles.png) -87px 0 no-repeat');
                            break;
                        case 4:
                            $block.css('background', 'url(images/tiles.png) 0 0 no-repeat');
                            break;
                    }
                    $block.css('left', 32 + blocOffsetX + (blocW + blocGap) * i);
                    $block.css('top', 32 + blocOffsetY + (blocH + blocGap) * j);

                    //генерируем бонус
                    var bonus = false;
                    var star = parseInt(Math.random() * 25);

                    var bonuses = [1, 3, 6, 9, 12, 15, 18];

                    if (bonuses.indexOf(star) !== -1) {
                        bonus = star;

                        var bonusClass;

                        switch (star) {
                            case 1:
                                bonusClass = "pl_winder";
                                break;
                            case 3:
                                bonusClass = "pl_narrow";
                                break;
                            case 6:
                                bonusClass = "speed_up";
                                break;
                            case 9:
                                bonusClass = "speed_down";
                                break;
                            case 12:
                                bonusClass = "super_ball";
                                break;
                            case 15:
                                bonusClass = "plus_life";
                                break;
                            case 18:
                                bonusClass = "plus_score";
                                break;
                        }

                        $scene.append('<div id="bonus' + blocId + '" class="' + bonusClass + '"></div>');

                        var $bonus = $('#bonus' + blocId);
                        $bonus.css('left', 58 + blocOffsetX + (blocW + blocGap) * i);
                        $bonus.css('top', 32 + blocOffsetY + (blocH + blocGap) * j);
                        //добавляем в массив бонус объект
                        Bonuses.push(new Bonus((58 + blocOffsetX + (blocW + blocGap) * i), (32 + blocOffsetY + (blocH + blocGap) * j), 35, 35, bonus, false, 'bonus' + blocId));
                    }

                    //добавляем в массив блок объект
                    Level.push(new Bloc((32 + blocOffsetX + (blocW + blocGap) * i), (32 + blocOffsetY + (blocH + blocGap) * j), blocW, blocH, levl[l][i][j], blocId, bonus));
                    k++;
                }
            }
        }
    }
}


// возвращаем платформу и шар исходную
function InitBallPlatform() {
    // если есть летящие блоки, уничтожим их
    for (i in Bonuses) {
        if (Bonuses[i].isMove) {
            $('#' + Bonuses[i].id).remove();
            Bonuses.splice(i, 1);
        }
    }

    oBall.x = 630;                                    // обновляем координаты шара по Х
    oBall.y = 648;                                    // обновляем координаты шара по У
    getrandom(-3, 3);                                 // вычисляем начальный случайный вектор направления по X
    oBall.dx = SpeedX;                                // обновляем смещение по Х
    oBall.dy = -6;                                    // обновляем смещение по У увеличивая скорость по у
    oBall.isSuper = false;                            // сбрасывает супер силу
    $('#ball').attr('src', 'images/shar.png');        // сбрасываем внешний вид шара
    isFire = false;                                   // устанавливаем мяч на платформу
    isFirstTouch = true;                              // старт игры по первому касанию
    clearInterval(Gametimer);                         // останавливаем таймер
    oPlatform.targetX = 565;                          // обновляем координаты Х
    oPlatform.y = 668;                                // обновляем координаты У
    oPlatform.w = 150;                                // обновляем ширину
    oPlatform.centerW = 60;                           // обновляем размер середины платформы
    $('#platform_c').css('width', oPlatform.centerW); // устанавливаем ширину платформы
}

// функция расчета минут и секунд
function gentime() {
    min = Math.floor(time / 60);
    sec = time % 60;
    if (min < 10)
        min = "0" + min;
    if (sec < 10)
        sec = "0" + sec;
}

//сброс игры
function gameReset() {
    for (i in Level) { //с блоками
        $('#bloc' + Level[i].id).remove();
    }
    Level.splice(0, Level.length);
    for (i in Bonuses) { //с блоками
        $('#' + Bonuses[i].id).remove();
    }
    Bonuses.splice(0, Bonuses.length);
    InitBallPlatform();
    LevelNum = 0; //первый уровень
    Score = 0; //очки
    currentScore = 0; //очки
    Life = 3; //жизни
    time = sec = min = 0;
    Level = [];
    Bonuses = [];
    InitLevel(patternLevels, LevelNum);
    drawInfo();
    clearInterval(istart);
    istart = setInterval(drawScene, fps);
    Pause = false;
    isFire = false;
}

//функция остановки игры
function gameStop(win) {

    bestScore = localStorage.getItem('bestscore');

    if (bestScore == null) {
        bestScore = 0;
    }
    if (win) {
        $('#game_over').html(tokens.win);
    }
    else {
        $('#game_over').html(tokens.gameOver);
    }
    $('#best_score').html(tokens.cScore + ': ' + bestScore);
    $('#current_score').html(tokens.uResult + ': ' + currentScore);
    $("#score").show();

    display = SCORE_SCREEN;

    if (bestScore < currentScore) {
        localStorage.setItem('bestscore', currentScore); //сохранение в лок хранилище
        $('#high_score').html(tokens.cScore + ': ' + currentScore);
        // fix 2018.08.02
        LastScore = currentScore;
    }
    initNavigation();
}

//функция обработки потери шара
function damage() {

    //sound2.play();// воспроизведения звука
    Life--; //вычитаем одну жизнь
    if (Life < 0) {
        gameStop(false);
        gameReset();
    }
    else {
        InitBallPlatform();
    }

}

//функция отрисовки текущей информации игры
function drawInfo() {
    //меняем значение в информационных блоках
    $('#Lifes').html(Life);
    var currentLev = LevelNum + 1;
    $('#Level').html(currentLev + '/' + patternLevels.length);
    //$('#Time')[0].innerHTML = tokens.time + ' ' + min + ':' + sec;
    $('#ScoreCur').html(currentScore);
}

//переход на новый уровень
function nextLevel() {
    LevelNum++;                             //увеличиваем уровень
    if (LevelNum > patternLevels.length) {  //если уровни закончились

        gameStop(true);
        gameReset();
    }
    else {
        var currentLev = LevelNum + 1;
        InitLevel(patternLevels, LevelNum); //инициализируем новый уровень
        InitBallPlatform();
    }
}

//функция проверки состояния блока после удара
function hit(obj, i) {
    typeBloc = 0;
    obj.t = obj.t - 1;                                  //отнимаем у блока 1 жизнь
    switch (obj.t) {                                    //проверяем кол-во жизней
        case 0:                                         //если не осталось
            //проверяем наличие бонуса
            if (obj.bonus != false) {
                for (var k in Bonuses) {
                    if ('bonus' + obj.id == Bonuses[k].id) {
                        Bonuses[k].isMove = true;
                        $('#' + Bonuses[k].id).css('display', 'block');
                    }
                }
            }
            //$('#bloc' + obj.id).fadeOut(0, function() {     //скрываем блок эффектом
            //$('#scene').append('<img src="images/explosion.gif" id="gif' + obj.id + '">');
            //var xc = obj.x - 60;
            //var yc = obj.y - 135;
            //$('#gif' + obj.id).css('top', yc + 'px').css('left', xc + 'px').css('position', 'absolute');
            $('#bloc' + obj.id).css('background', 'url(images/tiles.png) -348px 0 no-repeat');
            function deleteBloc() {
                $('#bloc' + obj.id).remove();               //по завершению эффекта удаляем блок
            }
            if (Level.length > 1) {
                setTimeout(deleteBloc, 200);
            }
            else {
                setTimeout(deleteBloc, 0);
            }
            //});
            Level.splice(i, 1);                         //удаляем объект из массива координат
            typeBloc = 1;
            return typeBloc;
            break;
        case 1:
            $('#bloc' + obj.id).css('background', 'url(images/tiles.png) -261px 0 no-repeat');
            typeBloc = 2;
            return typeBloc;
            break;
        case 2:
            $('#bloc' + obj.id).css('background', 'url(images/tiles.png) -174px 0 no-repeat');
            typeBloc = 3;
            return typeBloc;
            break;
        case 3:
            $('#bloc' + obj.id).css('background', 'url(images/tiles.png) -87px 0 no-repeat');
            typeBloc = 4;
            return typeBloc;
            break;
    }
}

// функция столкновения шарика с блоком
function collision_bloc(obj, ball, i) {
    var Balltop = ball.y;
    var Ballbottom = ball.y + ball.r + ball.r;
    var Ballleft = ball.x;
    var Ballright = ball.x + ball.r + ball.r;

    var Platformtop = obj.y;
    var Platformbottom = obj.y + obj.h;
    var Platformleft = obj.x;
    var Platformright = obj.x + obj.w;

    //обнаружение столкновения с платформой
    if (Balltop < Platformbottom && Ballbottom > Platformtop && Ballleft < Platformright - ball.dx && Ballright > Platformleft - ball.dx) {

        //если скорость больше нуля по Х
        if (ball.dx > 0 && ball.dy < 0) {
            if (Math.abs(obj.x - ball.x) < Math.abs(obj.y + obj.h - ball.y)) {

                if (!ball.isSuper) {
                    ball.dx = -ball.dx;  //меняем вектор направления
                }
                hit(obj, i);         //меняем блок после удара
            }
            else {
                if (!ball.isSuper) {
                    ball.dy = -ball.dy;  //меняем вектор направления
                }
                hit(obj, i);         //меняем блок после удара
            }
        }

        //если скорость больше нуля по Y
        else if (ball.dx < 0 && ball.dy > 0) {
            if (Math.abs(obj.x + obj.w - ball.x) < Math.abs(obj.y - ball.y)) {
                if (!ball.isSuper) {
                    ball.dx = -ball.dx;  //меняем вектор направления
                }
                hit(obj, i);         //меняем блок после удара
            }
            else {
                if (!ball.isSuper) {
                    ball.dy = -ball.dy;  //меняем вектор направления
                }
                hit(obj, i);         //меняем блок после удара
            }
        }

        //если скорость больше нуля
        else if (ball.dx > 0 && ball.dy > 0) {
            if (Math.abs(obj.x - ball.x) < Math.abs(obj.y - ball.y)) {
                if (!ball.isSuper) {
                    ball.dx = -ball.dx;  //меняем вектор направления
                }
                hit(obj, i);         //меняем блок после удара
            }
            else {
                if (!ball.isSuper) {
                    ball.dy = -ball.dy;  //меняем вектор направления
                }
                hit(obj, i);         //меняем блок после удара
            }
        }

        //если скорость меньше нуля
        else if (ball.dx < 0 && ball.dy < 0) {
            if (Math.abs(obj.x + obj.w - ball.x) < Math.abs(obj.y + obj.h - ball.y)) {
                if (!ball.isSuper) {
                    ball.dx = -ball.dx;  //меняем вектор направления
                }
                hit(obj, i);         //меняем блок после удара
            }
            else {
                if (!ball.isSuper) {
                    ball.dy = -ball.dy;  //меняем вектор направления
                }
                hit(obj, i);         //меняем блок после удара
            }
        }
        Score = typeBloc * 10;                                //увеличиваем количество уничтоженных блоков
        currentScore += Score;    //считаем очки
//        //$('#sound11').append(coll_sound.playState);
//        if (coll_sound.playState == 1) {
//            coll_sound.stop();// остановка звука
//            //coll_sound.play();// воспроизведения звука
//        }
//        else {
        //coll_sound.play();// воспроизведения звука

    }
}

// функция столкновения шарика с платформой
function collision_platform(obj, ball) {
    var Balltop = ball.y;
    var Ballbottom = ball.y + ball.r + ball.r;
    var Ballleft = ball.x;
    var Ballright = ball.x + ball.r + ball.r;

    var Platformtop = obj.y;
    var Platformbottom = obj.y + obj.h;
    var Platformleft = obj.x;
    var Platformright = obj.x + obj.w;

    //обнаружение столкновения с платформой
    if (Balltop < Platformbottom && Ballbottom > Platformtop && Ballleft < Platformright - ball.dx && Ballright > Platformleft - ball.dx) {

        //sound3.play();// воспроизведения звука
        //если скорость больше нуля
        if (ball.dx > 0 && ball.dy > 0) {
            if (ball.x + ball.r - obj.x <= 15) {    //если мяч попадает в край платформы до 15 пикс
                ball.dy = -ball.dy * 1.05;             //ускоряем его и отражаем обратно
                ball.dx = -ball.dx * 1.2;
            }
            else if (ball.x + ball.r - obj.x > 15 && ball.x + ball.r - obj.x <= 30) {   //если мяч попадает в край от 15 до 30 пикс
                ball.dy = -ball.dy * 1.02;                                                 //ускоряем его и отражаем обратно
                ball.dx = -ball.dx;
            }
            else if (Math.abs(obj.x - ball.x) < Math.abs(obj.y - ball.y)) { //если мяч попадает в остальной край
                ball.dx = -ball.dx;                                         //отражаем
            }
            else {                                                          //если попадает в боковой край
                ball.dy = -ball.dy;                                         //отражаем вниз
            }
        }
        //если скорость больше нуля по Y
        else if (ball.dx < 0 && ball.dy > 0) {
            if (obj.x + obj.w - ball.x + ball.r <= 15) {
                ball.dy = -ball.dy * 1.2;
                ball.dx = -ball.dx * 1.5;
            }
            else if (obj.x + obj.w - ball.x + ball.r > 15 && obj.x + obj.w - ball.x - ball.r <= 30) {
                ball.dy = -ball.dy * 1.2;
                ball.dx = -ball.dx;
            }
            else if (Math.abs(obj.x + obj.w - ball.x) < Math.abs(obj.y - ball.y)) {
                ball.dx = -ball.dx;
            }
            else {
                ball.dy = -ball.dy;
            }
        }
    }
}

// функция столкновения бонуса с платформой
function collision_bonus(obj, bonus) {

    var Bonusbottom = bonus.y + bonus.h;
    var Bonusleft = bonus.x;
    var Bonusright = bonus.x + bonus.w;

    var Platformtop = obj.y;
    var Platformbottom = obj.y + obj.h;
    var Platformleft = obj.x;
    var Platformright = obj.x + obj.w;

    //обнаружение столкновения бонуса с платформой
    if (Bonusbottom > Platformtop && Bonusleft < Platformright - 4 && Bonusright > Platformleft - 4) {
        //уничтожаем бонус и создаем реакцию на него
        return true;
    }
}

//функция обнаружение столкновения шарика со стенками
function collision_border(ball) {
    var Balltop = ball.y;
    var Ballbottom = ball.y + ball.r + ball.r;
    var Ballleft = ball.x;
    var Ballright = ball.x + ball.r + ball.r;

    if (Ballright > SceneOffsetX + SceneW || Ballleft < SceneOffsetX) {

        //sound3.play();// воспроизведения звука
        if (ball.y - Math.abs(ball.dy) < SceneOffsetY) { //модуль
            ball.dy = -ball.dy;
            ball.dx = -ball.dx;
        }
        else {
            ball.dx = -ball.dx;
        }
    }
    else if (Balltop < SceneOffsetY) {

        //sound3.play();// воспроизведения звука

        ball.dy = -ball.dy;
    }
    else if (Ballbottom > 720) {
        damage();
    }
}

// обновление всех элементов
function drawScene() {
    // расчет секунд и минут
    gentime();
    // отрисовка текущей информации игры
    drawInfo();

    if (isFire) {
        // запускаем шарик
        oBall.x += oBall.dx;
        oBall.y += oBall.dy;
        // проверяем координаты платформы, чтобы объект не уезжал сцену
        if (bRightBut) {
            oPlatform.targetX += 35;
            if (oPlatform.targetX < SceneX1) {
                oPlatform.targetX = SceneX1;
            }
            if (oPlatform.targetX + oPlatform.w > SceneX2) {
                oPlatform.targetX = SceneX2 - oPlatform.w;
            }
        }
        else if (bLeftBut) {
            oPlatform.targetX -= 35;
            if (oPlatform.targetX < SceneX1) {
                oPlatform.targetX = SceneX1;
            }
            if (oPlatform.targetX + oPlatform.w > SceneX2) {
                oPlatform.targetX = SceneX2 - oPlatform.w;
            }
        }
        bRightBut = false;
        bLeftBut = false;
    }
    else {
        if (bRightBut) {
            // передвигаем объекты кнопкой Вправо
            oPlatform.targetX += 35;
            oBall.x += 35;
            // проверяем координаты платформы или шара, чтобы объекты не уезжали сцену
            if (oPlatform.targetX < SceneX1) {
                oPlatform.targetX = SceneX1;
            }
            if (oPlatform.targetX + oPlatform.w > SceneX2) {
                oPlatform.targetX = SceneX2 - oPlatform.w;
            }
            if (oBall.x < SceneX1 + (oPlatform.w / 2) - oBall.r) {
                oBall.x = SceneX1 + (oPlatform.w / 2) - oBall.r;
            }
            if (oBall.x + oBall.r > SceneX2 - (oPlatform.w / 2)) {
                oBall.x = SceneX2 - oBall.r - (oPlatform.w / 2);
            }
        }
        else if (bLeftBut) {
            // передвигаем объекты кнопкой Влево
            oPlatform.targetX -= 35;
            oBall.x -= 35;
            // проверяем координаты платформы или шара, чтобы объекты не уезжали сцену
            if (oPlatform.targetX < SceneX1) {
                oPlatform.targetX = SceneX1;
            }
            if (oPlatform.targetX + oPlatform.w > SceneX2) {
                oPlatform.targetX = SceneX2 - oPlatform.w;
            }
            if (oBall.x < SceneX1 + (oPlatform.w / 2) - oBall.r) {
                oBall.x = SceneX1 + (oPlatform.w / 2) - oBall.r;
            }
            if (oBall.x + oBall.r > SceneX2 - (oPlatform.w / 2)) {
                oBall.x = SceneX2 - oBall.r - (oPlatform.w / 2);
            }
        }
        bRightBut = false;
        bLeftBut = false;
    }

    // изменение координат шара и платформы
    if (oPlatform.targetX !== oPlatform.x) {
        oPlatform.x += Math.floor((oPlatform.targetX - oPlatform.x) / 2);
        var platform = document.getElementById('platform');
        platform.style.left = oPlatform.x + 'px';
    }

    var ball = document.getElementById('ball');
    ball.style.left = oBall.x + 'px';
    ball.style.top = oBall.y + 'px';

    // изменение положения бонусов и столкновений
    for (var i in Bonuses) {
        //движение
        if (Bonuses[i].isMove == true) {
            $('#' + Bonuses[i].id).css('top', Bonuses[i].y += 4);
        }
        if (Bonuses[i].y > 720) {
            $('#' + Bonuses[i].id).remove();
            Bonuses.splice(i, 1);
        }
    }

    //проверка столкновений
    if (oBall.y + (oBall.r * 2) > 600) {
        collision_platform(oPlatform, oBall); //с платформой
    }
    for (i = 0; i < Level.length; i++) { //с блоками
        collision_bloc(Level[i], oBall, i);
    }
    //со стенками
    collision_border(oBall);

    for (i in Bonuses) {
        //столкновение бонуса и платформы
        if (collision_bonus(oPlatform, Bonuses[i])) {
            currentScore += 50; //любой бонус дает +50 очков
            switch (Bonuses[i].t) {
                case 1:
                    if (oPlatform.centerW < 210) {
                        oPlatform.centerW += 50;
                        oPlatform.w = oPlatform.centerW + 90;
                        $('#platform_c').css('width', oPlatform.centerW);
                    }
                    break;
                case 3:
                    if (oPlatform.centerW > 10) {
                        oPlatform.centerW -= 50;
                        oPlatform.w = oPlatform.centerW + 90;
                        $('#platform_c').css('width', oPlatform.centerW);
                    }
                    break;
                case 6:
                    if (Math.abs(oBall.dy) + 2 < 15) {
                        if (oBall.dy > 0) {
                            oBall.dy = oBall.dy + 2;
                        }
                        else {
                            oBall.dy = oBall.dy - 2;
                        }
                    }
                    break;
                case 9:
                    if (Math.abs(oBall.dy) - 2 > 5) {
                        if (oBall.dy > 0) {
                            oBall.dy = oBall.dy - 2;
                        }
                        else {
                            oBall.dy = oBall.dy + 2;
                        }
                    }
                    break;
                case 12:
                    oBall.isSuper = true;
                    $('#ball').attr('src', 'images/super_shar.png');
                    break;
                case 15:
                    Life++;
                    $('#Lifes').html(Life);
                    break;
                case 18:
                    currentScore += 1000;
                    $('#ScoreCur').html(currentScore);
                    break;
            }

            $('#' + Bonuses[i].id).remove();
            Bonuses.splice(i, 1);
        }
    }

    //проверка прохождения уровня
    if (Level.length === 0) {
        clearInterval(istart);
        function NextLevOffset() {
            istart = setInterval(drawScene, fps);
            nextLevel();
        }
        setTimeout(NextLevOffset, 200);
    }
}

/* управление мышкой или касаниями в игре */
function controlMouse() {

    var scene = document.getElementById('scene')

    /* обработчики касаний */
    scene.addEventListener('touchstart', function (e) {

    }, false);

    scene.addEventListener("touchmove", function (e) {
        var touch = e.touches[0] || e.changedTouches[0];
        moveEventHandler(touch.pageX)
    }, false);

    scene.addEventListener('touchend', function (e) {
        if (isFirstTouch) {
            clickEventHandler()
        }

        isFirstTouch = false
    }, false);

    /* обработчик события движения мыши */
    scene.addEventListener('mousemove', function (e) {
        moveEventHandler(e.pageX)
    }, false);

    /* обработчик события нажатия кнопок мыши */
    scene.addEventListener('mouseup', function (e) {
        clickEventHandler()
    }, false);
}

function moveEventHandler(pageX) {

    if (pageX > SceneX1 && pageX < SceneX2) {
        oPlatform.targetX = pageX - (oPlatform.w / 2);
        if (oPlatform.targetX < SceneX1) {
            oPlatform.targetX = SceneX1;
        }
        if (oPlatform.targetX + oPlatform.w > SceneX2) {
            oPlatform.targetX = SceneX2 - oPlatform.w;
        }
        if (!isFire) {
            oBall.x = pageX - oBall.r;
            if (oBall.x < SceneX1 + (oPlatform.w / 2) - oBall.r) {
                oBall.x = SceneX1 + (oPlatform.w / 2) - oBall.r;
            }
            if (oBall.x + oBall.r > SceneX2 - (oPlatform.w / 2)) {
                oBall.x = SceneX2 - oBall.r - (oPlatform.w / 2);
            }
        }
    }
}

function clickEventHandler() {
    if (!isFire) {
        isFire = true;
        Gametimer = setInterval(countTime, 1000); //таймер
    }
}

//управление клавиатурой
function controlKeyboard() {
    $(window).keydown(function(event) { // проверяем нажатие на пульте

        switch (event.keyCode) {
            case VK_LEFT: // клавиша влево
                if (display === GAMEPLAY_SCREEN) {
                    bLeftBut = true;
                }
                break;
            case VK_RIGHT: // клавиша вправо
                if (display === GAMEPLAY_SCREEN) {
                    bRightBut = true;
                }
                break;
            case VK_ENTER: // клавиша ок
                if (isFireConfirm) {
                    if (display === GAMEPLAY_SCREEN) {
                        if (!isFire) {
                            isFire = true;
                            Gametimer = setInterval(countTime, 1000); //пускаем таймер
                        }
                    }
                }
                if (display === GAMEPLAY_SCREEN) {
                    isFireConfirm = true;
                }
                break;
            case 80: // 'P' key VK_BACK || 80
                if (display === GAMEPLAY_SCREEN) {
                    restart()
                }
                break;
            case VK_BACK: // 'P' key VK_BACK || 80
                if (display === GAMEPLAY_SCREEN) {
                    restart()
                }
                else if (display === HELP_SCREEN) {
                    close_help();
                }
                else if (display === PAUSE_SCREEN) {
                    end_game();
                }
                else if (display === START_SCREEN) {
                    backHandler();
                }
                break;
        }

        /* Собираем 4 последние нажатые кнопки в массив */
        buttonsStack.push(event.keyCode)
        if (buttonsStack.length > 4) {
            buttonsStack.shift();
        }

        /* Чит-код на супер мяч */
        if (JSON.stringify(buttonsStack) === JSON.stringify(superBallButtonsOrder)) {
            oBall.dy = oBall.dy > 0 ? 30 : -30;
            oBall.isSuper = true;
            $('#ball').attr('src', 'images/super_shar.png');
        }

        /* Чит-код на широкую платформу */
        if (JSON.stringify(buttonsStack) === JSON.stringify(extraWidePlatformOrder)) {
            oPlatform.x = 30;
            oPlatform.centerW = 1120;
            oPlatform.w = oPlatform.centerW + 90;
            $('#platform_c').css('width', oPlatform.centerW);
        }
    });

    $(window).keyup(function(event) { // отжатие на пульте
        if (display === GAMEPLAY_SCREEN) {
            switch (event.keyCode) {
                case VK_LEFT: // 'Left' key
                    bLeftBut = false;
                    break;
                case VK_RIGHT: // 'Right' key
                    bRightBut = false;
                    break;
            }
        }
    });
}

//получение языка ТВ
function getSettings() {
    var device = document.getElementById("device");
    tvLanguage = device.tvLanguage2;

    var mac = localStorage.getItem('mac');
    var model = localStorage.getItem('model');

    var install;

    if (model == null || mac == null || model == undefined || mac == undefined) { //|| model == undefined || mac == undefined
        mac = device.modelName;
        model = device.net_macAddress;

        localStorage.setItem('mac', mac);
        localStorage.setItem('model', model);

        $.post('http://appsmarttv.ru/ad/', {app: '10', method: 'init', installed: true, lang: tvLanguage, mac: mac, model: model});
    }
    else {
        $.post('http://appsmarttv.ru/ad/', {app: '10', method: 'init', installed: false, lang: tvLanguage, mac: mac, model: model});
    }
}

//получение музыки
function change_music(action) {
    if (action) {
        isMusic = true;
        //sound_bg.currentTime = 0;
        //sound_bg.play();
        $('#music').attr('onclick', 'change_music(false);');
        $('#music').attr('class', 'music');
        initNavigation(true);
    }
    else {
        isMusic = false;
        //sound_bg.stop();
        $('#music').attr('onclick', 'change_music(true);');
        $('#music').attr('class', 'musicOff');
        initNavigation(true);
    }

}

//запуск музыки заново
function processPlayStateChangeFunction() {
    if (sound_bg.playState == 5) {
        // read and process playState property
        //sound_bg.play();
    }
}

//получение музыки
function getMusic() {
    //coll_sound = document.getElementById("coll_sound");
    //sound2 = document.getElementById("sound2");
    //sound3 = document.getElementById("sound3");
    //sound_bg = document.getElementById("sound_bg");
    //sound_bg.onPlayStateChange = processPlayStateChangeFunction;
}

function change_lang_en(lang) {

    if (lang) {
        tokens = {
            //меню
            game: '<strong>START GAME</strong>',
            cScore: '<strong>HIGH SCORE</strong>',
            pause: '<strong>PAUSE</strong>',
            continue: '<strong>CONTINUE</strong>',
            endgame: '<strong>END GAME</strong>',
            gameOver: '<strong>GAME OVER!</strong>',
            win: '<strong>YOU WIN!</strong>',
            uResult: '<strong>YOUR SCORE</strong>',
            newgame: '<strong>NEW GAME</strong>',
            closeHelp: '<strong>CLOSE</strong>',
            backText: '< Back'
        }
        $('#start_game').html(tokens.game);
        $('#high_score').html(tokens.cScore + ': ' + LastScore);
        $('#pause').html(tokens.pause);
        $('#cont').html(tokens.continue);
        $('.end_game').html(tokens.endgame);
        $('#game_again').html(tokens.newgame);
        $('.end_game').html(tokens.endgame);
        $('#close_help').html(tokens.closeHelp);
        $('.back-control').html(tokens.backText);

        $('#lang').attr('onclick', 'change_lang_en(false);');
        $('#help_ru').hide();
        $('#help_eng').show();

    }
    else {
        tokens = {
            //меню
            game: '<strong>НОВАЯ ИГРА</strong>',
            cScore: '<strong>РЕКОРД</strong>',
            pause: '<strong>ПАУЗА</strong>',
            continue: '<strong>ПРОДОЛЖИТЬ</strong>',
            endgame: '<strong>ВЫХОД</strong>',
            gameOver: '<strong>ВЫ ПРОИГРАЛИ!</strong>',
            win: '<strong>ВЫ ПОБЕДИЛИ!</strong>',
            uResult: '<strong>ВАШ РЕКОРД</strong>',
            newgame: '<strong>НОВАЯ ИГРА</strong>',
            closeHelp: '<strong>ЗАКРЫТЬ</strong>',
            backText: '< Назад'
        }
        $('#start_game').html(tokens.game);
        $('#high_score').html(tokens.cScore + ': ' + LastScore);
        $('#pause').html(tokens.pause);
        $('#cont').html(tokens.continue);
        $('.end_game').html(tokens.endgame);
        $('#game_again').html(tokens.newgame);
        $('.end_game').html(tokens.endgame);
        $('#close_help').html(tokens.closeHelp);
        $('.back-control').html(tokens.backText);

        $('#lang').attr('onclick', 'change_lang_en(true);');
        $('#help_ru').show();
        $('#help_eng').hide();
    }
}

//локализация
function initTokens() {
    if (tvLanguage == 'en') {
        change_lang_en(true);
    }
    else if (tvLanguage == 'ru') {
        change_lang_en(false);
    }
    else {
        tvLanguage = 'en';
        change_lang_en(true);
    }
}

//счетчик времени
function countTime() {
    time++;
}

//управление меню
//обработка меню интерфейса игры
function show_game() {
    if (display != 1) {

        $(".screen").hide();
        $("#game").show();

        display = GAMEPLAY_SCREEN;
    }
}

function end_game() {
    $(".screen").hide();
    $("#menu").show();
    gameReset();
    display = START_SCREEN;
    isFireConfirm = false;
    initNavigation();
}

function show_continue() {
    if (display !== GAMEPLAY_SCREEN) {
        $(".screen").hide();
        $("#game").show();

        clearInterval(istart);
        istart = setInterval(drawScene, fps);
        Gametimer = setInterval(countTime, 1000);

        Pause = false;
        isFireConfirm = false;

        display = GAMEPLAY_SCREEN;
    }
}

function game_again() {
    if (display !== GAMEPLAY_SCREEN) {
        $(".screen").hide();
        $("#game").show();
        Pause = false;
        isFireConfirm = false;
        display = GAMEPLAY_SCREEN;
    }
}

function close_help() {
    $("#help").hide();
    display = START_SCREEN;
    initNavigation();
}

function open_help() {
    $("#help").show();
    display = HELP_SCREEN;
    initNavigation();
}

function initNavigation(isMusic) {
    function findBlock(array, value) {
        for (var i = 0; i < array.length; i++) {
            if (array[i].blockId === value)
                return i;
        }
        return false;
    }
    //проверяем какая страница открыта
    if ($("#help").is(":visible")) {
        if (findBlock(NB.blockArray, 'Help') === false) {
            closeHelp = document.getElementById('close_help');
            NB.addBlock("Help", 0, Array(closeHelp));
        }
    }
    else if ($("#score").is(":visible")) {
        if (findBlock(NB.blockArray, 'Score') === false) {
            gameAgain = document.getElementById('game_again');
            endGameS = document.getElementById('end_gameS');
            NB.addBlock("Score", 0, Array(gameAgain, endGameS));
        }
    }
    else if ($("#continue").is(":visible")) {
        if (findBlock(NB.blockArray, 'Pause') === false) {
            cont = document.getElementById('cont');
            endGame = document.getElementById('end_gameP');
            NB.addBlock("Pause", 0, Array(cont, endGame));
        }
    }
    else if ($("#menu").is(":visible")) {
        if (findBlock(NB.blockArray, 'StartGame') === false) {
            langCh = document.getElementById('lang');
            startGame = document.getElementById('start_game');
            NB.addBlock("StartGame", 0, Array(langCh, startGame));
            backB = document.getElementById('back');
            infoB = document.getElementById('info');
            NB.addBlock("DownMenu", 1, Array(backB, infoB));

            if ((webOS && !webOS.platform.tv) || (navigator.userAgent.match(/(webOS.TV-\d+).+Compatible/)) !== null) {
                backB.style.display = 'block';
            }
        }

        NB.getBlockById("StartGame").setNextBlockLRUD(0, 0, 0, "DownMenu"); //relation from block to block
        NB.getBlockById("StartGame").setNextBlockWayLRUD(0, 0, 0, TO_FIRST); //TO_FIRST/TO_LAST/TO_SAME/0

        NB.getBlockById("DownMenu").setNextBlockLRUD(0, 0, "StartGame", 0); //relation from block to block
        NB.getBlockById("DownMenu").setNextBlockWayLRUD(0, 0, TO_LAST, 0); //TO_FIRST/TO_LAST/TO_SAME/0
    }

    switch (display) {
        case 0:
                NB.setFocusToElement("StartGame", startGame);
            break;
        case 2:
            NB.setFocusToElement("Help", closeHelp);
            break;
        case 4:
            NB.setFocusToElement("Score", gameAgain);
            break;
        case 5:
            NB.setFocusToElement("Pause", cont);
            break;
    }
    NB.initialMouseOverForAll();
}

function mouseonoff_handler() {
    status = window.NetCastGetMouseOnOff();
    return status;
}

function start() {
    $('#preloader').hide();
    $('#menu').show();

    getSettings();

    getMusic();

    //загрузка рекордного результата
    LastScore = localStorage.getItem('bestscore');
    if (LastScore == null) {
        LastScore = 0;
    }

    initTokens();

    change_music(true); //включаем музыку

    initNavigation();

    getrandom(-3, 3); //генерация скорости мяча по Х

    InitLevel(patternLevels, LevelNum); //инициализация уровня

    var ballImg = document.getElementById('ball');
    var platformImg = document.getElementById('platform');
    oBall = new Ball(640 - 20, 638, SpeedX, -6, 10, ballImg, false); //создаем объект шар
    oPlatform = new Platform(565, 668, 150, 20, platformImg, 60); //создаем объект платформу

    oBall.x = oBall.x + oBall.r;
    oBall.y = oBall.y + oBall.r;

    controlMouse(); //управление мышкой

    controlKeyboard(); //управление клавиатурой

    if (window.NetCastSetAutoMouseOff) {
        window.NetCastSetAutoMouseOff('disable');
    }

    istart = setInterval(drawScene, fps); //рисуем сцену

    //счетчик времени
    function countTime() {
        time++;
    }
}

function restart() {
    clearInterval(istart);
    clearInterval(Gametimer);
    Pause = true;
    $("#continue").show();
    display = PAUSE_SCREEN;
    initNavigation();
}

function backHandler() {
    if (!webOS.platform.tv || (navigator.userAgent.match(/(webOS.TV-\d+).+Compatible/)) !== null) {
        window.NetCastBack();
    } else {
        webOS.platformBack();
    }
}