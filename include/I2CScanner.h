#ifndef I2C_SCANNER_H
#define I2C_SCANNER_H

#include <Arduino.h>
#include <Wire.h>
#include <vector>

/**
 * @brief Classe para escaneamento e detecção de dispositivos I2C
 */
class I2CScanner {
public:
    /**
     * @brief Estrutura para informações de dispositivo I2C
     */
    struct I2CDevice {
        uint8_t address;        // Endereço I2C
        String deviceType;      // Tipo provável do dispositivo
        bool responding;        // Se está respondendo
    };

    /**
     * @brief Inicializa o scanner I2C
     * @param sda_pin Pino SDA
     * @param scl_pin Pino SCL
     * @param frequency Frequência I2C (padrão: 100kHz)
     */
    static void begin(int sda_pin = 21, int scl_pin = 22, uint32_t frequency = 100000);

    /**
     * @brief Escaneia todos os endereços I2C possíveis
     * @return Vector com dispositivos encontrados
     */
    static std::vector<I2CDevice> scanAll();

    /**
     * @brief Procura especificamente por PCF8574
     * @return Endereço do primeiro PCF8574 encontrado (0 se não encontrado)
     */
    static uint8_t findPCF8574();

    /**
     * @brief Procura por múltiplos PCF8574
     * @return Vector com endereços de todos os PCF8574 encontrados
     */
    static std::vector<uint8_t> findAllPCF8574();

    /**
     * @brief Verifica se um endereço específico está respondendo
     * @param address Endereço para testar
     * @return true se dispositivo responde
     */
    static bool testAddress(uint8_t address);

    /**
     * @brief Imprime resultado do scan no Serial
     */
    static void printScanResults();

    /**
     * @brief Obtém nome provável do dispositivo baseado no endereço
     * @param address Endereço I2C
     * @return String com tipo provável do dispositivo
     */
    static String getDeviceType(uint8_t address);

    /**
     * @brief Verifica se endereço está na faixa do PCF8574
     * @param address Endereço para verificar
     * @return true se está na faixa PCF8574 (0x20-0x27)
     */
    static bool isPCF8574Address(uint8_t address);

private:
    static std::vector<I2CDevice> lastScanResults;
};

#endif // I2C_SCANNER_H
