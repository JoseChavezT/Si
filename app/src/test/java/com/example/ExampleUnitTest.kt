package com.example

import org.junit.Assert.assertEquals
import org.junit.Test

class ExampleUnitTest {

    @Test
    fun testCalculatorBusinessRules() {
        val viewModel = CalculatorViewModel()

        // Case 0: Empty input
        viewModel.onQuantityChanged("")
        var state = viewModel.uiState.value
        assertEquals("", state.inputText)
        assertEquals(0L, state.quantity)
        assertEquals(0L, state.pallets)
        assertEquals(0L, state.remainder)

        // Case 1: 72 jabas -> 1 pallet, 0 remainder
        viewModel.onQuantityChanged("72")
        state = viewModel.uiState.value
        assertEquals("72", state.inputText)
        assertEquals(72L, state.quantity)
        assertEquals(1L, state.pallets)
        assertEquals(0L, state.remainder)

        // Case 2: 144 jabas -> 2 pallets, 0 remainder
        viewModel.onQuantityChanged("144")
        state = viewModel.uiState.value
        assertEquals("144", state.inputText)
        assertEquals(144L, state.quantity)
        assertEquals(2L, state.pallets)
        assertEquals(0L, state.remainder)

        // Case 3: 800 jabas -> 11 pallets, 8 remainder
        viewModel.onQuantityChanged("800")
        state = viewModel.uiState.value
        assertEquals("800", state.inputText)
        assertEquals(800L, state.quantity)
        assertEquals(11L, state.pallets)
        assertEquals(8L, state.remainder)

        // Case 4: 865 jabas -> 12 pallets, 1 remainder (singular jaba)
        viewModel.onQuantityChanged("865")
        state = viewModel.uiState.value
        assertEquals("865", state.inputText)
        assertEquals(865L, state.quantity)
        assertEquals(12L, state.pallets)
        assertEquals(1L, state.remainder)

        // Case 5: 70 jabas -> 0 pallets, 70 remainder
        viewModel.onQuantityChanged("70")
        state = viewModel.uiState.value
        assertEquals("70", state.inputText)
        assertEquals(70L, state.quantity)
        assertEquals(0L, state.pallets)
        assertEquals(70L, state.remainder)
    }

    @Test
    fun testInputSanitization() {
        val viewModel = CalculatorViewModel()

        // Filter out non-numeric characters
        viewModel.onQuantityChanged("8a0-0_")
        var state = viewModel.uiState.value
        assertEquals("800", state.inputText)
        assertEquals(800L, state.quantity)

        // Leading zeros drop while preserving single 0
        viewModel.onQuantityChanged("000")
        state = viewModel.uiState.value
        assertEquals("0", state.inputText)
        assertEquals(0L, state.quantity)

        viewModel.onQuantityChanged("0072")
        state = viewModel.uiState.value
        assertEquals("72", state.inputText)
        assertEquals(72L, state.quantity)
    }
}
