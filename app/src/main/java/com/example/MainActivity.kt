package com.example

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Archive
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.Inventory
import androidx.compose.material.icons.outlined.ChatBubble
import androidx.compose.material.icons.outlined.Info
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.example.ui.theme.MyApplicationTheme
import com.example.ui.theme.AmoledBackground
import com.example.ui.theme.AmoledSurface
import com.example.ui.theme.GreenPrimary
import com.example.ui.theme.GreenDark
import com.example.ui.theme.TextWhite
import com.example.ui.theme.TextSecondary
import com.example.ui.theme.SubtleBorder
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {
    private val viewModel: CalculatorViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            MyApplicationTheme {
                Scaffold(
                    modifier = Modifier
                        .fillMaxSize()
                        .background(AmoledBackground),
                    contentWindowInsets = WindowInsets.safeDrawing
                ) { innerPadding ->
                    CalculatorScreen(
                        viewModel = viewModel,
                        modifier = Modifier.padding(innerPadding)
                    )
                }
            }
        }
    }
}

@Composable
fun CalculatorScreen(
    viewModel: CalculatorViewModel,
    modifier: Modifier = Modifier
) {
    val uiState by viewModel.uiState.collectAsStateWithLifecycle()
    val targetColor = when {
        uiState.quantity <= 150L -> Color(0xFF9E9E9E)   // Gris
        uiState.quantity <= 500L -> Color(0xFF16C784)   // Verde
        uiState.quantity <= 1000L -> Color(0xFF00E676)  // Verde intenso
        uiState.quantity <= 1600L -> Color(0xFFFFD600)  // Amarillo
        uiState.quantity <= 2200L -> Color(0xFFFF9100)  // Naranja
        else -> Color(0xFFFF3D00)                       // Rojo
    }
    val animatedColor by animateColorAsState(
        targetValue = targetColor,
        animationSpec = tween(durationMillis = 300),
        label = "DynamicColorAnimation"
    )
    val currentLoadLevel = when {
        uiState.quantity <= 150L -> 0
        uiState.quantity <= 500L -> 1
        uiState.quantity <= 1000L -> 2
        uiState.quantity <= 1600L -> 3
        uiState.quantity <= 2200L -> 4
        else -> 5
    }
    val haptic = LocalHapticFeedback.current
    var previousLoadLevel by remember { mutableStateOf<Int?>(null) }
    LaunchedEffect(currentLoadLevel) {
        if (previousLoadLevel != null && previousLoadLevel != currentLoadLevel) {
            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
        }
        previousLoadLevel = currentLoadLevel
    }
    val focusRequester = remember { FocusRequester() }
    val keyboardController = LocalSoftwareKeyboardController.current
    val focusManager = LocalFocusManager.current
    val coroutineScope = rememberCoroutineScope()

    // Bouncy scale animation state for the result when CALCULAR is pressed
    val resultScale = remember { Animatable(1f) }

    // Request keyboard focus automatically on start
    LaunchedEffect(Unit) {
        delay(150)
        focusRequester.requestFocus()
        keyboardController?.show()
    }

    BoxWithConstraints(
        modifier = modifier
            .fillMaxSize()
            .background(AmoledBackground)
    ) {
        val screenHeight = maxHeight
        val isSmallScreen = screenHeight < 680.dp

        val isScrollable = isSmallScreen || uiState.history.isNotEmpty()

        Column(
            modifier = Modifier
                .fillMaxSize()
                .then(
                    if (isScrollable) Modifier.verticalScroll(rememberScrollState())
                    else Modifier
                )
                .padding(horizontal = 24.dp, vertical = 20.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = if (isScrollable) Arrangement.Top else Arrangement.SpaceBetween
        ) {
            // ==========================================
            // PARTE SUPERIOR (Resultado y Tarjetas)
            // ==========================================
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = if (isSmallScreen) 12.dp else 24.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                // Header Title
                Text(
                    text = "RESULTADO",
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 2.sp,
                    color = TextSecondary,
                    modifier = Modifier.padding(bottom = 8.dp)
                )

                // Giant Animated Result Row
                ResultDisplay(
                    pallets = uiState.pallets,
                    remainder = uiState.remainder,
                    scale = resultScale.value,
                    textColor = animatedColor
                )

                Spacer(modifier = Modifier.height(12.dp))

                // Humorous phrase section with fade animation in real-time
                AnimatedContent(
                    targetState = uiState.humorousPhrase,
                    transitionSpec = {
                        fadeIn(animationSpec = tween(400)) togetherWith fadeOut(animationSpec = tween(400))
                    },
                    label = "HumorousPhraseAnimation",
                    modifier = Modifier.fillMaxWidth()
                ) { phrase ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(color = AmoledSurface, shape = RoundedCornerShape(16.dp))
                            .border(BorderStroke(1.dp, SubtleBorder), shape = RoundedCornerShape(16.dp))
                            .padding(horizontal = 16.dp, vertical = 12.dp)
                            .testTag("humorous_card"),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.Center
                    ) {
                        Icon(
                            imageVector = Icons.Outlined.ChatBubble,
                            contentDescription = null,
                            tint = animatedColor,
                            modifier = Modifier
                                .size(18.dp)
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            text = phrase,
                            fontSize = 13.sp,
                            fontWeight = FontWeight.Medium,
                            color = TextSecondary,
                            textAlign = TextAlign.Center,
                            lineHeight = 18.sp,
                            modifier = Modifier.weight(1f, fill = false)
                        )
                    }
                }
            }

            if (isSmallScreen) {
                Spacer(modifier = Modifier.height(24.dp))
            }

            // ==========================================
            // CENTRO (Entrada de datos)
            // ==========================================
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = if (isSmallScreen) 12.dp else 24.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Text(
                    text = "Cantidad de jabas",
                    fontSize = 16.sp,
                    fontWeight = FontWeight.Medium,
                    color = TextSecondary,
                    modifier = Modifier.padding(bottom = 8.dp)
                )

                // Huge centered TextField
                OutlinedTextField(
                    value = uiState.inputText,
                    onValueChange = { viewModel.onQuantityChanged(it) },
                    modifier = Modifier
                        .fillMaxWidth()
                        .focusRequester(focusRequester)
                        .testTag("quantity_input"),
                    textStyle = LocalTextStyle.current.copy(
                        textAlign = TextAlign.Center,
                        fontSize = 52.sp,
                        fontWeight = FontWeight.Bold,
                        color = TextWhite
                    ),
                    placeholder = {
                        Text(
                            text = "0",
                            modifier = Modifier.fillMaxWidth(),
                            textAlign = TextAlign.Center,
                            fontSize = 52.sp,
                            fontWeight = FontWeight.Bold,
                            color = SubtleBorder
                        )
                    },
                    keyboardOptions = KeyboardOptions(
                        keyboardType = KeyboardType.Number,
                        imeAction = ImeAction.Done
                    ),
                    keyboardActions = KeyboardActions(
                        onDone = {
                            keyboardController?.hide()
                            focusManager.clearFocus()
                            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                            viewModel.saveToHistory()
                            coroutineScope.launch {
                                resultScale.animateTo(1.15f, tween(150, easing = FastOutSlowInEasing))
                                resultScale.animateTo(1.0f, spring(Spring.DampingRatioMediumBouncy, Spring.StiffnessLow))
                            }
                        }
                    ),
                    singleLine = true,
                    trailingIcon = {
                        if (uiState.inputText.isNotEmpty()) {
                            IconButton(
                                onClick = {
                                    viewModel.onQuantityChanged("")
                                    haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                                },
                                modifier = Modifier
                                    .size(48.dp)
                                    .testTag("clear_text_button")
                            ) {
                                Icon(
                                    imageVector = Icons.Filled.Clear,
                                    contentDescription = "Borrar texto",
                                    tint = TextSecondary,
                                    modifier = Modifier.size(24.dp)
                                )
                            }
                        }
                    },
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedBorderColor = Color.Transparent,
                        unfocusedBorderColor = Color.Transparent,
                        disabledBorderColor = Color.Transparent,
                        errorBorderColor = Color.Transparent,
                        cursorColor = GreenPrimary
                    )
                )

                // Helper/Info badge
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.Center,
                    modifier = Modifier.padding(top = 12.dp)
                ) {
                    Icon(
                        imageVector = Icons.Outlined.Info,
                        contentDescription = null,
                        tint = GreenPrimary,
                        modifier = Modifier.size(16.dp)
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        text = "1 pallet = 72 jabas",
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Medium,
                        color = TextSecondary
                    )
                }
            }

            if (isSmallScreen) {
                Spacer(modifier = Modifier.height(24.dp))
            }

            // ==========================================
            // PARTE INFERIOR (Botones de acción)
            // ==========================================
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = if (isSmallScreen) 16.dp else 24.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                // Button CALCULAR (primary green)
                Button(
                    onClick = {
                        keyboardController?.hide()
                        focusManager.clearFocus()
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        viewModel.saveToHistory()
                        coroutineScope.launch {
                            resultScale.animateTo(1.15f, tween(150, easing = FastOutSlowInEasing))
                            resultScale.animateTo(1.0f, spring(Spring.DampingRatioMediumBouncy, Spring.StiffnessLow))
                        }
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = GreenPrimary,
                        contentColor = AmoledBackground
                    ),
                    shape = RoundedCornerShape(24.dp),
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(64.dp)
                        .testTag("submit_button")
                ) {
                    Text(
                        text = "CALCULAR",
                        fontSize = 18.sp,
                        fontWeight = FontWeight.Bold,
                        letterSpacing = 1.sp
                    )
                }

                Spacer(modifier = Modifier.height(14.dp))

                // Button LIMPIAR (secondary dark gray)
                Button(
                    onClick = {
                        viewModel.clear()
                        haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                        focusRequester.requestFocus()
                        keyboardController?.show()
                    },
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFF1C1C1E),
                        contentColor = TextWhite
                    ),
                    border = BorderStroke(1.dp, SubtleBorder),
                    shape = RoundedCornerShape(24.dp),
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(56.dp)
                        .testTag("clear_button")
                ) {
                    Text(
                        text = "LIMPIAR",
                        fontSize = 16.sp,
                        fontWeight = FontWeight.Bold,
                        color = TextSecondary
                    )
                }

                // Recent History List
                if (uiState.history.isNotEmpty()) {
                    Spacer(modifier = Modifier.height(28.dp))
                    
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "HISTORIAL RECIENTE",
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Bold,
                            letterSpacing = 2.sp,
                            color = TextSecondary
                        )
                        TextButton(
                            onClick = {
                                viewModel.clearHistory()
                                haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                            },
                            modifier = Modifier.testTag("clear_history_button")
                        ) {
                            Text(
                                text = "BORRAR",
                                fontSize = 11.sp,
                                fontWeight = FontWeight.Bold,
                                color = GreenPrimary
                            )
                        }
                    }
                    
                    Spacer(modifier = Modifier.height(10.dp))
                    
                    uiState.history.forEach { item ->
                        HistoryItemRow(
                            item = item,
                            onItemClick = {
                                viewModel.loadFromHistory(item)
                                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                coroutineScope.launch {
                                    resultScale.animateTo(1.15f, tween(150, easing = FastOutSlowInEasing))
                                    resultScale.animateTo(1.0f, spring(Spring.DampingRatioMediumBouncy, Spring.StiffnessLow))
                                }
                            }
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ResultDisplay(
    pallets: Long,
    remainder: Long,
    scale: Float,
    textColor: Color
) {
    Box(
        modifier = Modifier
            .fillMaxWidth()
            .scale(scale)
            .padding(vertical = 12.dp),
        contentAlignment = Alignment.Center
    ) {
        // Subtle and elegant halo behind the result text
        Box(
            modifier = Modifier
                .size(width = 300.dp, height = 120.dp)
                .background(
                    brush = Brush.radialGradient(
                        colors = listOf(
                            textColor.copy(alpha = 0.08f),
                            Color.Transparent
                        )
                    )
                )
        )

        FlowRow(
            horizontalArrangement = Arrangement.Center,
            verticalArrangement = Arrangement.Bottom,
            maxItemsInEachRow = Int.MAX_VALUE,
            modifier = Modifier.fillMaxWidth()
        ) {
            if (pallets > 0L) {
                // Pallets Number
                AnimatedNumberText(value = pallets, fontSize = 54.sp, color = textColor)
                
                // Pallets Label
                val palletsLabel = if (pallets == 1L) " pallet" else " pallets"
                Text(
                    text = palletsLabel,
                    fontSize = 24.sp,
                    fontWeight = FontWeight.Bold,
                    color = textColor,
                    modifier = Modifier
                        .padding(end = 4.dp)
                        .offset(y = 24.dp)
                )

                if (remainder > 0L) {
                    // Separator plus sign
                    Text(
                        text = " + ",
                        fontSize = 24.sp,
                        fontWeight = FontWeight.Bold,
                        color = textColor.copy(alpha = 0.6f),
                        modifier = Modifier
                            .padding(start = 6.dp, end = 6.dp)
                    )

                    // Remainder Number
                    AnimatedNumberText(value = remainder, fontSize = 54.sp, color = textColor)

                    // Remainder Label
                    val remainderLabel = if (remainder == 1L) " jaba" else " jabas"
                    Text(
                        text = remainderLabel,
                        fontSize = 24.sp,
                        fontWeight = FontWeight.Bold,
                        color = textColor,
                        modifier = Modifier
                            .offset(y = 24.dp)
                    )
                }
            } else {
                // Only remainder (jabas) when pallets == 0 (including 0 -> 0 jabas)
                // Remainder Number
                AnimatedNumberText(value = remainder, fontSize = 54.sp, color = textColor)

                // Remainder Label
                val remainderLabel = if (remainder == 1L) " jaba" else " jabas"
                Text(
                    text = remainderLabel,
                    fontSize = 24.sp,
                    fontWeight = FontWeight.Bold,
                    color = textColor,
                    modifier = Modifier
                        .offset(y = 24.dp)
                )
            }
        }
    }
}

@Composable
fun AnimatedNumberText(
    value: Long,
    fontSize: androidx.compose.ui.unit.TextUnit,
    color: Color,
    modifier: Modifier = Modifier
) {
    AnimatedContent(
        targetState = value,
        transitionSpec = {
            if (targetState > initialState) {
                (slideInVertically { height -> height } + fadeIn() togetherWith
                        slideOutVertically { height -> -height } + fadeOut())
            } else {
                (slideInVertically { height -> -height } + fadeIn() togetherWith
                        slideOutVertically { height -> height } + fadeOut())
            }.using(
                SizeTransform(clip = false)
            )
        },
        label = "AnimatedNumber",
        modifier = modifier
    ) { targetValue ->
        Text(
            text = targetValue.toString(),
            fontSize = fontSize,
            fontWeight = FontWeight.Black,
            color = color,
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(horizontal = 2.dp)
        )
    }
}

@Composable
fun HistoryItemRow(
    item: HistoryItem,
    onItemClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    Card(
        onClick = onItemClick,
        modifier = modifier
            .fillMaxWidth()
            .testTag("history_item_${item.id}"),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(
            containerColor = AmoledSurface
        ),
        border = BorderStroke(1.dp, SubtleBorder)
    ) {
        Row(
            modifier = Modifier
                .padding(14.dp)
                .fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(
                    text = "${item.quantity} jabas",
                    fontSize = 16.sp,
                    fontWeight = FontWeight.Bold,
                    color = TextWhite
                )
                Text(
                    text = item.timestamp,
                    fontSize = 11.sp,
                    color = TextSecondary
                )
            }
            
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                val palletsText = if (item.pallets == 1L) "1 pallet" else "${item.pallets} pallets"
                val remainderText = if (item.remainder > 0L) " + ${item.remainder} jabas" else ""
                
                Text(
                    text = palletsText,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Bold,
                    color = GreenPrimary
                )
                if (remainderText.isNotEmpty()) {
                    Text(
                        text = remainderText,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Medium,
                        color = TextWhite
                    )
                }
            }
        }
    }
}


