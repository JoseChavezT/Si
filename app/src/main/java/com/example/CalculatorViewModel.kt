package com.example

import androidx.lifecycle.ViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

data class HistoryItem(
    val id: Long,
    val quantity: Long,
    val pallets: Long,
    val remainder: Long,
    val timestamp: String
)

data class CalculatorUiState(
    val inputText: String = "",
    val quantity: Long = 0L,
    val pallets: Long = 0L,
    val remainder: Long = 0L,
    val humorousPhrase: String = "¡El almacén está vacío! ¿Salió refrigerio?",
    val history: List<HistoryItem> = emptyList()
)

class CalculatorViewModel : ViewModel() {

    private val _uiState = MutableStateFlow(CalculatorUiState())
    val uiState: StateFlow<CalculatorUiState> = _uiState.asStateFlow()

    private var lastPhrase = "¡El almacén está vacío! ¿Salió refrigerio?"

    private val phrasesZero = listOf(
        "¡El almacén está vacío! ¿Salió refrigerio?",
        "Cero jabas... el montacargas está durmiendo plácidamente.",
        "Almacén en cero. ¡La gente de despacho se fue a almorzar su menú de luca!",
        "No hay carga hoy. ¡Hora de sacar el celular y disimular trabajando!"
    )

    private val phrasesPucho = listOf(
        "¡Ese pucho de jabas se carga al hombro nomás, sin llorar!",
        "Ni un pallet completo... el supervisor te está mirando con cara de dudas.",
        "Ese sencillo lo mueves con transpaleta manual al toque, sin sudar.",
        "Con fe, unas jabas más y armamos el primer pallet completo.",
        "¡Tranquilo choche, ese pucho sale volando en un ratito!"
    )

    private val phrasesSmall = listOf(
        "¡Saliendo despacho express! Prende el montacargas de una vez.",
        "Un par de pallets y ya podemos ir cobrando la quincena bien fría.",
        "Tranquilo, esto se acomoda al toque entre pata y pata.",
        "¡Ese cargador está con todas las pilas hoy, se come la cancha!",
        "Ya va agarrando cuerpo el camión de reparto."
    )

    private val phrasesMedium = listOf(
        "¡Ya se llenó el camioncito! Llama al chofer que despierte de su siesta.",
        "¡Asu, harto lomo para cargar hoy día! Fuerza en esos brazos.",
        "La gente de seguridad ya está alistando las guías de remisión a toda máquina.",
        "El supervisor ya está sonriendo, hoy de todas maneras sale su bono.",
        "A paso firme. ¡Ese montacargas ya está pidiendo su recompensa!"
    )

    private val phrasesHuge = listOf(
        "¡Se viene el contenedor completo! ¡Preparen el café cargado o el Red Bull!",
        "¡Esto es nivel Leyenda del Almacén! Mis respetos, patrón de los pallets.",
        "Llama a todo el barrio y al sindicato para descargar este tremendo tráiler.",
        "¡Madre mía, hoy nos quedamos a sobretiempo de cajón!",
        "¡Ese almacén ya parece mercado mayorista de tanta mercadería!"
    )

    private val phrasesOverLimit = listOf(
        "¡Asu, te pasaste de vueltas! Esa cantidad ya supera la capacidad de todo el Almacén Central.",
        "¿Tantas jabas? ¡Vas a necesitar como diez montacargas o llamar a los Transformers!",
        "¡Esa carga supera lo habitual! Ni el patrón de los pallets tiene tanto espacio en la zona de despacho.",
        "¡Tranquilo choche, que vas a hundir el camión con ese cerro de mercadería!",
        "¿Más de 2500 jabas? ¡Mejor llama a la Marina de Guerra para mover todo esto!"
    )

    fun onQuantityChanged(input: String) {
        // Validation: Only allow digits 0-9
        val filtered = input.filter { it.isDigit() }
        
        // Remove leading zeros, unless it is just "0"
        val cleanInput = when {
            filtered.isEmpty() -> ""
            filtered.all { it == '0' } -> "0"
            else -> filtered.dropWhile { it == '0' }
        }

        val quantity = cleanInput.toLongOrNull() ?: 0L
        val pallets = quantity / 72
        val remainder = quantity % 72
        
        // Select humorous phrase based on range without repeating the last one
        val phraseList = when {
            quantity == 0L -> phrasesZero
            quantity < 72L -> phrasesPucho
            quantity < 360L -> phrasesSmall
            quantity < 1440L -> phrasesMedium
            quantity <= 2500L -> phrasesHuge
            else -> phrasesOverLimit
        }
        val availablePhrases = phraseList.filter { it != lastPhrase }
        val phrase = if (availablePhrases.isNotEmpty()) availablePhrases.random() else phraseList.random()
        lastPhrase = phrase

        _uiState.update { currentState ->
            currentState.copy(
                inputText = cleanInput,
                quantity = quantity,
                pallets = pallets,
                remainder = remainder,
                humorousPhrase = phrase
            )
        }
    }

    fun saveToHistory() {
        val state = _uiState.value
        if (state.quantity == 0L) return

        val timestamp = SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date())
        val newItem = HistoryItem(
            id = System.currentTimeMillis(),
            quantity = state.quantity,
            pallets = state.pallets,
            remainder = state.remainder,
            timestamp = timestamp
        )

        _uiState.update { currentState ->
            val currentHistory = currentState.history
            // Avoid duplicate consecutive entries
            if (currentHistory.isNotEmpty() && currentHistory.first().quantity == state.quantity) {
                return@update currentState
            }
            val newHistory = (listOf(newItem) + currentHistory).take(5)
            currentState.copy(history = newHistory)
        }
    }

    fun loadFromHistory(item: HistoryItem) {
        onQuantityChanged(item.quantity.toString())
    }

    fun clearHistory() {
        _uiState.update { currentState ->
            currentState.copy(history = emptyList())
        }
    }

    fun clear() {
        _uiState.update { currentState ->
            currentState.copy(
                inputText = "",
                quantity = 0L,
                pallets = 0L,
                remainder = 0L,
                humorousPhrase = phrasesZero.random()
            )
        }
    }
}
