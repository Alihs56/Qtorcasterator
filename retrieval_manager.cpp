#include "retrieval_manager.h"
#include "vector_db.h"
#include "embedding_client.h"
#include "pdf_processor.h"
#include "logger.h"
#include <QRegularExpression>

RetrievalManager::RetrievalManager(ApiClient *api, VectorDB *db,
                                   EmbeddingClient *embedder, PdfProcessor *pdfProc,
                                   QObject *parent)
    : QObject(parent), m_api(api), m_db(db), m_embedder(embedder), m_pdfProc(pdfProc)
{
}

void RetrievalManager::retrieve(const QString &query, bool needRag,
                                std::function<void(const RetrievalResult&)> callback)
{
    if (!needRag || !m_db->isReady()) {
        callback({});
        return;
    }

    QString expandedQuery = expandQuery(query);

    LOG_INFO("Retrieval", QString("Query: %1 → Expanded: %2").arg(query.left(50)).arg(expandedQuery.left(80)));

    m_embedder->getEmbedding(expandedQuery, [this, query, expandedQuery, callback](const QVector<float> &vec) {
        if (vec.isEmpty()) {
            LOG_WARN("Retrieval", "Embedding failed for query");
            callback({});
            return;
        }

        QString context = m_db->getContext(expandedQuery, vec, 50, 4000);
        context = compressContext(context, 3000);

        RetrievalResult result;
        result.context = context;
        result.tokensUsed = context.length() / 4;

        LOG_INFO("Retrieval", QString("Context: %1 chars, ~%2 tokens")
                             .arg(context.length()).arg(result.tokensUsed));

        callback(result);
        emit retrievalComplete(result);
    });
}

void RetrievalManager::ingestPdf(const QString &filepath,
                                 std::function<void(bool, int, int)> callback)
{
    if (!m_pdfProc) {
        if (callback) callback(false, 0, 0);
        return;
    }

    m_pdfProc->processPdf(filepath, [this, callback](const PdfProcessingResult &res) {
        if (callback) callback(res.success, res.totalChunks, res.totalPages);
        if (res.success)
            emit pdfIngested(res.filename, res.totalChunks, res.totalPages);
        else
            emit retrievalError(res.errorMessage);
    });
}

void RetrievalManager::clear()
{
    if (m_db)
        m_db->clearAll();
}

QString RetrievalManager::expandQuery(const QString &query) const
{
    QString expanded = query;
    QString q = query.toLower();

    auto addTerms = [&](const QString &terms) {
        expanded += " " + terms;
    };

    if (q.contains("usart") || q.contains("uart") || q.contains("serial") ||
        q.contains(QString::fromUtf8("سریال")) || q.contains(QString::fromUtf8("رجیستر"))) {
        addTerms("USART UART Serial Communication TX RX UBRR UDR UCSR1A UCSR1B UCSR1C register address");
    }

    if (q.contains("adc") || q.contains("analog") || q.contains(QString::fromUtf8("آنالوگ")) ||
        q.contains(QString::fromUtf8("مبدل"))) {
        addTerms("ADC ADMUX ADCSRA ADCSRB ADCH ADCL SFIOR conversion reference");
    }

    if (q.contains("timer") || q.contains("counter") || q.contains(QString::fromUtf8("تایمر")) ||
        q.contains(QString::fromUtf8("کانتر"))) {
        addTerms("Timer Counter TCCR TCNT OCR ICR TIMSK TIFR PWM");
    }

    if (q.contains("interrupt") || q.contains(QString::fromUtf8("وقفه"))) {
        addTerms("Interrupt ISR Interrupt Vector Enable Flag");
    }

    if (q.contains("spi")) {
        addTerms("SPI SPCR SPSR SPDR MOSI MISO SCK");
    }

    if (q.contains("i2c") || q.contains("twi")) {
        addTerms("TWI I2C TWBR TWCR TWDR TWAR");
    }

    if (q.contains("eeprom")) {
        addTerms("EEPROM EEAR EEDR EECR");
    }

    if (q.contains("port") || q.contains(QString::fromUtf8("پورت"))) {
        addTerms("PORT PIN DDR GPIO");
    }

    if (q.contains("frequency") || q.contains("clock") || q.contains(QString::fromUtf8("فرکانس"))) {
        addTerms("Clock Frequency Crystal Oscillator MHz F_CPU");
    }

    return expanded;
}

QString RetrievalManager::compressContext(const QString &context, int maxTokens) const
{
    if (context.length() / 4 <= maxTokens)
        return context;

    int maxChars = maxTokens * 4;
    QString compressed = context.left(maxChars);

    int lastNewline = compressed.lastIndexOf('\n');
    if (lastNewline > maxChars * 0.8)
        compressed = compressed.left(lastNewline);

    return compressed.trimmed();
}
