QT += core gui network sql widgets

CONFIG += c++17

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = QtAiOrchestrator
TEMPLATE = app

HEADERS += \
    MainWindow.h \
    CodeEditor.h \
    SyntaxHighlighter.h \
    ProjectExplorer.h \
    logger.h \
    process_manager.h \
    api_client.h \
    vram_manager.h \
    settings_manager.h \
    session_manager.h \
    smart_chunker.h \
    orchestrator.h \
    pdf_processor.h \
    build_pipeline.h \
    vector_db.h \
    embedding_client.h \
    terminal_executor.h \
    git_manager.h \
    system_tray.h \
    settings_dialog.h \
    monitor_widget.h \
    git_panel.h \
    memory_manager.h \
    planning_engine.h \
    retrieval_manager.h \
    context_manager.h \
    prompt_builder.h \
    tool_manager.h \
    execution_engine.h \
    reviewer.h \
    language_intel.h \
    LanguageDetector.h \
    CodeParser.h \
    SymbolDatabase.h \
    CallGraph.h \
    DependencyGraph.h \
    CodeIndexer.h \
    VectorStorageManager.h \
    Retriever.h \
    Reranker.h \
    ContextBuilder.h \
    ContextCompressor.h \
    CodeVerifier.h \
    ProjectIndexer.h \
    CodeModificationEngine.h \
    GitBackupLayer.h \
    ProjectWorkspaceManager.h \
    FileContextResolver.h \
    ModificationController.h \
    GitBackupManager.h

SOURCES += \
    main.cpp \
    MainWindow.cpp \
    ProjectExplorer.cpp \
    logger.cpp \
    process_manager.cpp \
    api_client.cpp \
    vram_manager.cpp \
    settings_manager.cpp \
    session_manager.cpp \
    smart_chunker.cpp \
    orchestrator.cpp \
    pdf_processor.cpp \
    build_pipeline.cpp \
    vector_db.cpp \
    embedding_client.cpp \
    terminal_executor.cpp \
    git_manager.cpp \
    system_tray.cpp \
    settings_dialog.cpp \
    monitor_widget.cpp \
    git_panel.cpp \
    memory_manager.cpp \
    planning_engine.cpp \
    retrieval_manager.cpp \
    context_manager.cpp \
    prompt_builder.cpp \
    tool_manager.cpp \
    execution_engine.cpp \
    reviewer.cpp \
    language_intel.cpp \
    LanguageDetector.cpp \
    CodeParser.cpp \
    SymbolDatabase.cpp \
    CallGraph.cpp \
    DependencyGraph.cpp \
    CodeIndexer.cpp \
    VectorStorageManager.cpp \
    Retriever.cpp \
    Reranker.cpp \
    ContextBuilder.cpp \
    ContextCompressor.cpp \
    CodeVerifier.cpp \
    ProjectIndexer.cpp \
    CodeModificationEngine.cpp \
    GitBackupLayer.cpp \
    ProjectWorkspaceManager.cpp \
    FileContextResolver.cpp \
    ModificationController.cpp \
    GitBackupManager.cpp

target.path = /usr/local/bin
INSTALLS += target
